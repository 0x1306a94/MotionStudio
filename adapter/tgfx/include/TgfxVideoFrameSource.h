#pragma once

#include <memory>

#include "MotionStudio/export/VideoFrameSource.h"

namespace motion {

// Offscreen tgfx frame source that renders into IOSurface-backed CVPixelBuffers
// for zero-copy handoff to AVAssetWriter. Forces cornerRadius = 0.
class TgfxVideoFrameSource : public VideoFrameSource {
  public:
    TgfxVideoFrameSource();
    ~TgfxVideoFrameSource() override;

    void setPlatformPixelBufferPool(void *pool) override;

    // Returns nullptr-constructed usability: prepare fails when Metal is unavailable.
    Expected<void, std::string> prepare(const Document &document, EntityId compositionId,
                                        const VideoExportOptions &options) override;
    Expected<VideoFrame, std::string> renderFrame(FrameTime time) override;
    void finish() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace motion
