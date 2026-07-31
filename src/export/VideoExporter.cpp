#include "MotionStudio/export/VideoExporter.h"

namespace motion {

Expected<void, std::string> VideoExporter::Export(
    const Document &, EntityId, const VideoExportOptions &, VideoFrameSource &, VideoEncoder &,
    const std::function<bool(VideoExportProgress)> &) {
    return Unexpected<std::string>("not implemented");
}

}  // namespace motion
