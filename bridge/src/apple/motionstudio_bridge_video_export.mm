#include "motionstudio_bridge.h"

#include <cstdlib>
#include <cstring>
#include <string>

#include "AvfVideoEncoder.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/export/VideoExportOptions.h"
#include "MotionStudio/export/VideoExporter.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "TgfxVideoFrameSource.h"
#include "common/DocumentLock.h"

namespace {

void SetError(char **errorOut, const std::string &message) {
    if (errorOut == nullptr) {
        return;
    }
    *errorOut = strdup(message.c_str());
}

motion::H264Profile MapProfile(int profile) {
    switch (profile) {
        case 0:
            return motion::H264Profile::Baseline;
        case 1:
            return motion::H264Profile::Main;
        default:
            return motion::H264Profile::High;
    }
}

}  // namespace

bool ms_video_export(MSDocument *document, uint64_t compositionId,
                     const MSVideoExportOptions *options,
                     bool (*progress)(void *ctx, int64_t completed, int64_t total), void *progressCtx,
                     const volatile int *cancelFlag, char **errorOut) {
    if (errorOut != nullptr) {
        *errorOut = nullptr;
    }
    if (document == nullptr) {
        SetError(errorOut, "document is null");
        return false;
    }
    if (options == nullptr || options->outputPath == nullptr || options->outputPath[0] == '\0') {
        SetError(errorOut, "output path is empty");
        return false;
    }

    DocumentLock lock(document);
    if (document->document == nullptr) {
        SetError(errorOut, "document is null");
        return false;
    }

    const motion::EntityId compositionEntity{compositionId};
    const motion::Composition *composition =
        document->document->entityIndex().findComposition(compositionEntity);
    if (composition == nullptr) {
        SetError(errorOut, "composition not found");
        return false;
    }

    motion::VideoExportOptions exportOptions;
    exportOptions.outputPath = options->outputPath;
    const int64_t start = options->startFrame < 0 ? 0 : options->startFrame;
    const int64_t end = options->endFrame < 0 ? composition->duration : options->endFrame;
    exportOptions.range = {start, end};
    exportOptions.width = options->width;
    exportOptions.height = options->height;
    exportOptions.frameRate.num = options->frameRateNum > 0 ? static_cast<uint32_t>(options->frameRateNum) : 0;
    exportOptions.frameRate.den = options->frameRateDen > 0 ? static_cast<uint32_t>(options->frameRateDen) : 0;
    exportOptions.bitrateBps = options->bitrateBps;
    exportOptions.keyframeInterval = options->keyframeInterval;
    exportOptions.profile = MapProfile(options->profile);

    motion::TgfxVideoFrameSource source;
    motion::AvfVideoEncoder encoder;
    const auto result = motion::VideoExporter::Export(
        *document->document, motion::EntityId{compositionId}, exportOptions, source, encoder,
        [&](motion::VideoExportProgress exportProgress) {
            if (cancelFlag != nullptr && *cancelFlag != 0) {
                return false;
            }
            if (progress == nullptr) {
                return true;
            }
            return progress(progressCtx, exportProgress.completedFrames, exportProgress.totalFrames);
        });
    if (!result.hasValue()) {
        SetError(errorOut, result.error());
        return false;
    }
    return true;
}
