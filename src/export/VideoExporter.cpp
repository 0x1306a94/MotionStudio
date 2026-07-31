#include "MotionStudio/export/VideoExporter.h"

#include <algorithm>
#include <cmath>

#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"

namespace motion {
namespace {

bool IsPositiveEven(int value) {
    return value > 0 && (value % 2) == 0;
}

Expected<VideoExportOptions, std::string> Resolve(const Document &document, EntityId compositionId,
                                                  const VideoExportOptions &options) {
    const Composition *composition = document.entityIndex().findComposition(compositionId);
    if (composition == nullptr) {
        return Unexpected<std::string>("composition not found");
    }
    if (options.outputPath.empty()) {
        return Unexpected<std::string>("output path is empty");
    }

    VideoExportOptions resolved = options;
    if (resolved.width == 0) {
        resolved.width = composition->width;
    }
    if (resolved.height == 0) {
        resolved.height = composition->height;
    }
    if (!IsPositiveEven(resolved.width) || !IsPositiveEven(resolved.height)) {
        return Unexpected<std::string>("export size must be positive even dimensions");
    }

    if (resolved.frameRate.den == 0) {
        resolved.frameRate = composition->frameRate;
    }
    if (resolved.frameRate.num == 0 || resolved.frameRate.den == 0) {
        return Unexpected<std::string>("invalid frame rate");
    }

    if (resolved.range.end <= resolved.range.start) {
        resolved.range = {0, composition->duration};
    }
    if (resolved.range.start < 0 || resolved.range.end > composition->duration ||
        resolved.range.end <= resolved.range.start) {
        return Unexpected<std::string>("invalid export range");
    }

    const double fps =
        static_cast<double>(resolved.frameRate.num) / static_cast<double>(resolved.frameRate.den);
    if (resolved.bitrateBps <= 0) {
        const double raw =
            static_cast<double>(resolved.width) * static_cast<double>(resolved.height) * fps * 0.1;
        resolved.bitrateBps = static_cast<int>(std::clamp(raw, 1000000.0, 50000000.0));
    }
    if (resolved.keyframeInterval <= 0) {
        resolved.keyframeInterval = std::max(1, static_cast<int>(std::lround(fps * 2.0)));
    }
    return resolved;
}

}  // namespace

Expected<void, std::string> VideoExporter::Export(
    const Document &document, EntityId compositionId, const VideoExportOptions &options,
    VideoFrameSource &frames, VideoEncoder &encoder,
    const std::function<bool(VideoExportProgress)> &onProgress) {
    auto resolved = Resolve(document, compositionId, options);
    if (!resolved.hasValue()) {
        return Unexpected<std::string>(resolved.error());
    }

    auto prepared = frames.prepare(document, compositionId, *resolved);
    if (!prepared.hasValue()) {
        return Unexpected<std::string>(prepared.error());
    }

    auto begun = encoder.begin(*resolved);
    if (!begun.hasValue()) {
        frames.finish();
        return Unexpected<std::string>(begun.error());
    }

    const FrameTime start = resolved->range.start;
    const FrameTime end = resolved->range.end;
    const FrameTime total = end - start;
    for (FrameTime time = start; time < end; ++time) {
        VideoExportProgress progress{time - start, total};
        if (onProgress && !onProgress(progress)) {
            encoder.abort();
            frames.finish();
            return Unexpected<std::string>("cancelled");
        }
        auto frame = frames.renderFrame(time);
        if (!frame.hasValue()) {
            encoder.abort();
            frames.finish();
            return Unexpected<std::string>(frame.error());
        }
        auto appended = encoder.appendFrame(*frame, time - start);
        if (frame->releaseHandle != nullptr && frame->platformHandle != nullptr) {
            frame->releaseHandle(frame->platformHandle);
        }
        if (!appended.hasValue()) {
            encoder.abort();
            frames.finish();
            return Unexpected<std::string>(appended.error());
        }
    }

    auto ended = encoder.end();
    frames.finish();
    if (!ended.hasValue()) {
        encoder.abort();
        return Unexpected<std::string>(ended.error());
    }
    return Expected<void, std::string>();
}

}  // namespace motion
