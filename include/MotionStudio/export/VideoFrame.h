#pragma once

#include <cstddef>
#include <cstdint>

namespace motion {

enum class VideoFrameStorage { CpuRgba,
                               PlatformShared };

struct VideoFrame {
    int width = 0;
    int height = 0;
    VideoFrameStorage storage = VideoFrameStorage::CpuRgba;
    const uint8_t *rgba = nullptr;
    size_t rowBytes = 0;
    bool premultiplied = true;
    void *platformHandle = nullptr;
    void (*retainHandle)(void *) = nullptr;
    void (*releaseHandle)(void *) = nullptr;
};

}  // namespace motion
