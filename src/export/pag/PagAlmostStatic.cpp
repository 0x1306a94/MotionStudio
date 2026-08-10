#include "PagAlmostStatic.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace motion {
namespace pag_export {
namespace {

constexpr float kAlmostStaticChangedRatio = 0.001f;

float ChangedPixelRatio(const uint8_t *previous, const uint8_t *current, int width, int height,
                        size_t previousRowBytes, size_t currentRowBytes) {
    if (previous == nullptr || current == nullptr || width <= 0 || height <= 0) {
        return 1.0f;
    }
    const int64_t totalPixels = static_cast<int64_t>(width) * static_cast<int64_t>(height);
    if (totalPixels <= 0) {
        return 1.0f;
    }
    int64_t changed = 0;
    for (int y = 0; y < height; ++y) {
        const auto *prevRow = reinterpret_cast<const uint32_t *>(
            static_cast<const void *>(previous + static_cast<size_t>(y) * previousRowBytes));
        const auto *curRow = reinterpret_cast<const uint32_t *>(
            static_cast<const void *>(current + static_cast<size_t>(y) * currentRowBytes));
        for (int x = 0; x < width; ++x) {
            if (prevRow[x] != curRow[x]) {
                ++changed;
            }
        }
    }
    return static_cast<float>(changed) / static_cast<float>(totalPixels);
}

std::vector<FrameTime> SampleTimes(FrameTime start, FrameTime end) {
    std::vector<FrameTime> times;
    if (end <= start) {
        return times;
    }
    const FrameTime last = end - 1;
    times.push_back(start);
    if (last == start) {
        return times;
    }
    const FrameTime mid = start + (last - start) / 2;
    if (mid != start && mid != last) {
        times.push_back(mid);
    }
    // Up to two additional uniform samples between start and last.
    const FrameTime span = last - start;
    if (span >= 3) {
        const FrameTime t1 = start + span / 3;
        const FrameTime t2 = start + (2 * span) / 3;
        for (FrameTime t : {t1, t2}) {
            if (t != start && t != mid && t != last) {
                times.push_back(t);
            }
        }
    }
    times.push_back(last);
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());
    return times;
}

}  // namespace

bool IsAlmostStaticSequence(BitmapFrameSource *source, FrameTime start, FrameTime end) {
    if (source == nullptr || end <= start) {
        return false;
    }
    const std::vector<FrameTime> times = SampleTimes(start, end);
    if (times.size() < 2) {
        return true;
    }

    Expected<BitmapFrame, std::string> first = source->renderFrame(times.front());
    if (!first.hasValue()) {
        return false;
    }
    const int width = first.value().width;
    const int height = first.value().height;
    if (width <= 0 || height <= 0 || first.value().rgba == nullptr) {
        return false;
    }
    const size_t stride = static_cast<size_t>(width) * 4u;
    std::vector<uint8_t> previous(stride * static_cast<size_t>(height));
    std::vector<uint8_t> current(stride * static_cast<size_t>(height));
    for (int y = 0; y < height; ++y) {
        std::memcpy(previous.data() + static_cast<size_t>(y) * stride,
                    first.value().rgba + static_cast<size_t>(y) * first.value().rowBytes, stride);
    }

    for (size_t index = 1; index < times.size(); ++index) {
        Expected<BitmapFrame, std::string> rendered = source->renderFrame(times[index]);
        if (!rendered.hasValue()) {
            return false;
        }
        const BitmapFrame &frame = rendered.value();
        if (frame.width != width || frame.height != height || frame.rgba == nullptr) {
            return false;
        }
        for (int y = 0; y < height; ++y) {
            std::memcpy(current.data() + static_cast<size_t>(y) * stride,
                        frame.rgba + static_cast<size_t>(y) * frame.rowBytes, stride);
        }
        const float ratio =
            ChangedPixelRatio(previous.data(), current.data(), width, height, stride, stride);
        if (ratio >= kAlmostStaticChangedRatio) {
            return false;
        }
        std::swap(previous, current);
    }
    return true;
}

}  // namespace pag_export
}  // namespace motion
