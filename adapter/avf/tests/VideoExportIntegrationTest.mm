#include <filesystem>
#include <memory>

#include <gtest/gtest.h>

#import <AVFoundation/AVFoundation.h>

#include "AvfVideoEncoder.h"
#include "MotionStudio/export/VideoExportOptions.h"
#include "MotionStudio/export/VideoExporter.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "TgfxVideoFrameSource.h"

using motion::AvfVideoEncoder;
using motion::Composition;
using motion::Document;
using motion::TgfxVideoFrameSource;
using motion::VideoExportOptions;
using motion::VideoExporter;

TEST(VideoExportIntegrationTest, ExportsShortClip) {
    Document document;
    auto composition = std::make_unique<Composition>();
    composition->width = 64;
    composition->height = 64;
    composition->duration = 2;
    composition->frameRate = {30, 1};
    composition->backgroundColor = {0, 1, 0, 1};
    composition->cornerRadius = 16.0f;
    document.addComposition(std::move(composition));

    const auto path =
        (std::filesystem::temp_directory_path() / "motionstudio_export_e2e.mp4").string();
    std::filesystem::remove(path);

    VideoExportOptions options;
    options.outputPath = path;
    options.width = 64;
    options.height = 64;
    options.frameRate = {30, 1};
    options.bitrateBps = 1000000;
    options.keyframeInterval = 30;
    options.range = {0, 2};

    TgfxVideoFrameSource source;
    AvfVideoEncoder encoder;
    const auto result =
        VideoExporter::Export(document, document.compositions[0]->id, options, source, encoder);
    if (!result.hasValue() && result.error().find("Metal") != std::string::npos) {
        GTEST_SKIP() << result.error();
    }
    ASSERT_TRUE(result.hasValue()) << result.error();
    ASSERT_TRUE(std::filesystem::exists(path));
    ASSERT_GT(std::filesystem::file_size(path), 0u);

    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:url options:nil];
    EXPECT_GT(CMTimeGetSeconds(asset.duration), 0.0);
    std::filesystem::remove(path);
}
