#pragma once

#include <memory>
#include <string>

#include "MotionStudio/export/VideoEncoder.h"

namespace motion {

// H.264 + MP4 encoder backed by AVAssetWriter.
class AvfVideoEncoder : public VideoEncoder {
  public:
    AvfVideoEncoder();
    ~AvfVideoEncoder() override;

    Expected<void, std::string> begin(const VideoExportOptions &options) override;
    Expected<void, std::string> appendFrame(const VideoFrame &frame,
                                            FrameTime presentationIndex) override;
    Expected<void, std::string> end() override;
    void abort() override;
    void *platformPixelBufferPool() const override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace motion
