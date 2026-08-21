#include <filesystem>
#include <vector>

#include <gtest/gtest.h>

#import <AVFoundation/AVFoundation.h>

#include "AvfVideoEncoder.h"
#include "MotionStudio/export/VideoExportOptions.h"

using motion::AvfVideoEncoder;
using motion::H264Profile;
using motion::VideoContainer;
using motion::VideoExportOptions;
using motion::VideoFrame;
using motion::VideoFrameStorage;

namespace {

VideoFrame MakeRedFrame(std::vector<uint8_t> *rgba, int width, int height) {
    rgba->assign(static_cast<size_t>(width * height * 4), 0);
    for (size_t i = 0; i < rgba->size(); i += 4) {
        (*rgba)[i] = 255;
        (*rgba)[i + 3] = 255;
    }
    VideoFrame frame;
    frame.width = width;
    frame.height = height;
    frame.storage = VideoFrameStorage::CpuRgba;
    frame.rgba = rgba->data();
    frame.rowBytes = width * 4;
    frame.premultiplied = true;
    return frame;
}

VideoExportOptions MakeOptions(const std::string &path) {
    VideoExportOptions options;
    options.outputPath = path;
    options.width = 64;
    options.height = 64;
    options.frameRate = {30, 1};
    options.bitrateBps = 1000000;
    options.keyframeInterval = 30;
    options.profile = H264Profile::High;
    options.range = {0, 3};
    return options;
}

std::string EncodeThreeFrames(AvfVideoEncoder *encoder, const VideoExportOptions &options) {
    auto begun = encoder->begin(options);
    if (!begun.hasValue()) {
        return begun.error();
    }
    std::vector<uint8_t> rgba;
    const VideoFrame frame = MakeRedFrame(&rgba, 64, 64);
    for (int i = 0; i < 3; ++i) {
        const auto appended = encoder->appendFrame(frame, i);
        if (!appended.hasValue()) {
            return appended.error();
        }
    }
    auto ended = encoder->end();
    if (!ended.hasValue()) {
        return ended.error();
    }
    return {};
}

}  // namespace

TEST(AvfVideoEncoderTest, WritesMp4FromCpuFrames) {
    const auto path = (std::filesystem::temp_directory_path() / "motionstudio_avf_smoke.mp4").string();
    std::filesystem::remove(path);

    AvfVideoEncoder encoder;
    const std::string error = EncodeThreeFrames(&encoder, MakeOptions(path));
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_TRUE(std::filesystem::exists(path));
    ASSERT_GT(std::filesystem::file_size(path), 0u);

    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:url options:nil];
    EXPECT_GT(asset.duration.timescale, 0);
    std::filesystem::remove(path);
}

TEST(AvfVideoEncoderTest, WritesMovFromCpuFrames) {
    const auto path = (std::filesystem::temp_directory_path() / "motionstudio_avf_smoke.mov").string();
    std::filesystem::remove(path);

    AvfVideoEncoder encoder;
    VideoExportOptions options = MakeOptions(path);
    options.container = VideoContainer::Mov;
    options.optimizeForNetworkUse = true;
    const std::string error = EncodeThreeFrames(&encoder, options);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_TRUE(std::filesystem::exists(path));
    ASSERT_GT(std::filesystem::file_size(path), 0u);

    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:url options:nil];
    EXPECT_GT(asset.duration.timescale, 0);
    std::filesystem::remove(path);
}

TEST(AvfVideoEncoderTest, WritesMp4WithNetworkOptimize) {
    const auto path = (std::filesystem::temp_directory_path() / "motionstudio_avf_faststart.mp4").string();
    std::filesystem::remove(path);

    AvfVideoEncoder encoder;
    VideoExportOptions options = MakeOptions(path);
    options.optimizeForNetworkUse = true;
    const std::string error = EncodeThreeFrames(&encoder, options);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_TRUE(std::filesystem::exists(path));
    ASSERT_GT(std::filesystem::file_size(path), 0u);
    std::filesystem::remove(path);
}

TEST(AvfVideoEncoderTest, AbortRemovesPartialFile) {
    const auto path = (std::filesystem::temp_directory_path() / "motionstudio_avf_abort.mp4").string();
    std::filesystem::remove(path);
    AvfVideoEncoder encoder;
    VideoExportOptions options;
    options.outputPath = path;
    options.width = 64;
    options.height = 64;
    options.frameRate = {30, 1};
    options.bitrateBps = 1000000;
    options.keyframeInterval = 30;
    ASSERT_TRUE(encoder.begin(options).hasValue());
    encoder.abort();
    EXPECT_FALSE(std::filesystem::exists(path));
}
