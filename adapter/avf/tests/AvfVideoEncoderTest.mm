#include <filesystem>
#include <vector>

#include <gtest/gtest.h>

#import <AVFoundation/AVFoundation.h>

#include "AvfVideoEncoder.h"
#include "MotionStudio/export/VideoExportOptions.h"

using motion::AvfVideoEncoder;
using motion::H264Profile;
using motion::VideoExportOptions;
using motion::VideoFrame;
using motion::VideoFrameStorage;

TEST(AvfVideoEncoderTest, WritesMp4FromCpuFrames) {
    const auto path =
        (std::filesystem::temp_directory_path() / "motionstudio_avf_smoke.mp4").string();
    std::filesystem::remove(path);

    AvfVideoEncoder encoder;
    VideoExportOptions options;
    options.outputPath = path;
    options.width = 64;
    options.height = 64;
    options.frameRate = {30, 1};
    options.bitrateBps = 1000000;
    options.keyframeInterval = 30;
    options.profile = H264Profile::High;
    options.range = {0, 3};

    ASSERT_TRUE(encoder.begin(options).hasValue()) << "begin failed";
    std::vector<uint8_t> rgba(static_cast<size_t>(64 * 64 * 4), 0);
    for (size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = 255;
        rgba[i + 3] = 255;
    }
    VideoFrame frame;
    frame.width = 64;
    frame.height = 64;
    frame.storage = VideoFrameStorage::CpuRgba;
    frame.rgba = rgba.data();
    frame.rowBytes = 64 * 4;
    frame.premultiplied = true;
    for (int i = 0; i < 3; ++i) {
        const auto appended = encoder.appendFrame(frame, i);
        ASSERT_TRUE(appended.hasValue()) << appended.error();
    }
    const auto ended = encoder.end();
    ASSERT_TRUE(ended.hasValue()) << ended.error();
    ASSERT_TRUE(std::filesystem::exists(path));
    ASSERT_GT(std::filesystem::file_size(path), 0u);

    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:url options:nil];
    EXPECT_GT(asset.duration.timescale, 0);
    std::filesystem::remove(path);
}

TEST(AvfVideoEncoderTest, AbortRemovesPartialFile) {
    const auto path =
        (std::filesystem::temp_directory_path() / "motionstudio_avf_abort.mp4").string();
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
