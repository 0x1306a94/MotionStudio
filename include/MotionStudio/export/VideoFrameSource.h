#pragma once

#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Expected.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/export/VideoExportOptions.h"
#include "MotionStudio/export/VideoFrame.h"

namespace motion {

class Document;

class VideoFrameSource {
  public:
    virtual ~VideoFrameSource() = default;
    virtual Expected<void, std::string> prepare(const Document &document, EntityId compositionId,
                                                const VideoExportOptions &options) = 0;
    virtual Expected<VideoFrame, std::string> renderFrame(FrameTime time) = 0;
    virtual void finish() = 0;
};

}  // namespace motion
