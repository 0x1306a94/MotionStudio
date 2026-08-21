#pragma once

#include <memory>
#include <string>

#include "MotionStudio/export/VideoEncoder.h"

namespace motion {

// H.264 encoder backed by AVAssetWriter (MP4 or QuickTime).
class AvfVideoEncoder : public VideoEncoder {
  public:
    AvfVideoEncoder();
    ~AvfVideoEncoder() override;

    Expected<void, std::string> begin(const VideoExportOptions &options) override;
    Expected<void, std::string> appendFrame(const VideoFrame &frame,
                                            FrameTime presentationIndex) override;
    Expected<void, std::string> end() override;
    void abort() override;
    Expected<void, std::string> waitUntilReadyForMoreFrames() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace motion
