#pragma once

#include <functional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Expected.h"
#include "MotionStudio/export/VideoEncoder.h"
#include "MotionStudio/export/VideoExportOptions.h"
#include "MotionStudio/export/VideoFrameSource.h"

namespace motion {

class Document;

class VideoExporter {
  public:
    static Expected<void, std::string> Export(
        const Document &document, EntityId compositionId, const VideoExportOptions &options,
        VideoFrameSource &frames, VideoEncoder &encoder,
        const std::function<bool(VideoExportProgress)> &onProgress = {});
};

}  // namespace motion
