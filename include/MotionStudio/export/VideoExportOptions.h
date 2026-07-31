#pragma once

#include <string>

#include "MotionStudio/common/Time.h"

namespace motion {

enum class H264Profile { Baseline,
                         Main,
                         High };

struct VideoExportOptions {
    std::string outputPath;
    TimeRange range;
    int width = 0;
    int height = 0;
    FrameRate frameRate;
    int bitrateBps = 0;
    int keyframeInterval = 0;
    H264Profile profile = H264Profile::High;
};

struct VideoExportProgress {
    FrameTime completedFrames = 0;
    FrameTime totalFrames = 0;
};

}  // namespace motion
