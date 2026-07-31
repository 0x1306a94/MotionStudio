#pragma once

#include <string>

#include "MotionStudio/common/Expected.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/export/VideoExportOptions.h"
#include "MotionStudio/export/VideoFrame.h"

namespace motion {

class VideoEncoder {
  public:
    virtual ~VideoEncoder() = default;
    virtual Expected<void, std::string> begin(const VideoExportOptions &options) = 0;
    virtual Expected<void, std::string> appendFrame(const VideoFrame &frame,
                                                    FrameTime presentationIndex) = 0;
    virtual Expected<void, std::string> attachAudio() {
        return Unexpected<std::string>("audio not implemented");
    }
    virtual Expected<void, std::string> end() = 0;
    virtual void abort() = 0;
};

}  // namespace motion
