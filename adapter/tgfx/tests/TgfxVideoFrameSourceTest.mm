#include <memory>

#include <gtest/gtest.h>

#import <CoreVideo/CoreVideo.h>

#include "MotionStudio/export/VideoExportOptions.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "TgfxVideoFrameSource.h"

using motion::Composition;
using motion::Document;
using motion::TgfxVideoFrameSource;
using motion::VideoExportOptions;
using motion::VideoFrameStorage;

namespace {

struct BgraPixel {
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t a;
};

BgraPixel SampleCorner(CVPixelBufferRef buffer, size_t x, size_t y) {
    const size_t stride = CVPixelBufferGetBytesPerRow(buffer);
    const auto *base = static_cast<const uint8_t *>(CVPixelBufferGetBaseAddress(buffer));
    const uint8_t *pixel = base + y * stride + x * 4;
    return {pixel[0], pixel[1], pixel[2], pixel[3]};
}

}  // namespace

TEST(TgfxVideoFrameSourceTest, RendersPlatformSharedIgnoringCornerRadius) {
    Document document;
    auto composition = std::make_unique<Composition>();
    composition->width = 64;
    composition->height = 64;
    composition->duration = 1;
    composition->frameRate = {30, 1};
    composition->backgroundColor = {0, 1, 0, 1};
    composition->cornerRadius = 20.0f;
    document.addComposition(std::move(composition));

    TgfxVideoFrameSource source;
    VideoExportOptions options;
    options.width = 64;
    options.height = 64;
    options.frameRate = {30, 1};
    options.outputPath = "/tmp/unused.mp4";
    options.range = {0, 1};

    const auto prepared = source.prepare(document, document.compositions[0]->id, options);
    if (!prepared.hasValue()) {
        GTEST_SKIP() << prepared.error();
    }

    auto frame = source.renderFrame(0);
    ASSERT_TRUE(frame.hasValue()) << frame.error();
    EXPECT_EQ(frame->storage, VideoFrameStorage::PlatformShared);
    ASSERT_NE(frame->platformHandle, nullptr);

    CVPixelBufferRef buffer = static_cast<CVPixelBufferRef>(frame->platformHandle);
    EXPECT_EQ(CVPixelBufferGetWidth(buffer), 64u);
    EXPECT_EQ(CVPixelBufferGetHeight(buffer), 64u);

    ASSERT_EQ(CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly), kCVReturnSuccess);
    const BgraPixel topLeft = SampleCorner(buffer, 0, 0);
    const BgraPixel bottomRight = SampleCorner(buffer, 63, 63);
    CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);

    // Opaque green background at corners proves cornerRadius was ignored.
    EXPECT_EQ(topLeft.a, 255);
    EXPECT_EQ(bottomRight.a, 255);
    EXPECT_GT(topLeft.g, 200);
    EXPECT_GT(bottomRight.g, 200);
    EXPECT_LT(topLeft.r, 40);
    EXPECT_LT(topLeft.b, 40);

    if (frame->releaseHandle != nullptr) {
        frame->releaseHandle(frame->platformHandle);
    }
    source.finish();
}
