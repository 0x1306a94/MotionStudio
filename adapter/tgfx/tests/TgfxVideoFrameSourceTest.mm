#include <memory>

#include <gtest/gtest.h>

#import <CoreVideo/CoreVideo.h>

#include "MotionStudio/export/VideoExportOptions.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/LayerStylePaint.h"
#include "MotionStudio/model/LayerType.h"
#include "MotionStudio/model/ShaderDefinition.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeRect.h"
#include "TgfxVideoFrameSource.h"

using motion::BindShaderPaint;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::FillStyle;
using motion::Layer;
using motion::LayerType;
using motion::ShaderDefinition;
using motion::ShapeContent;
using motion::ShapeRect;
using motion::TgfxVideoFrameSource;
using motion::Vec2;
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

TEST(TgfxVideoFrameSourceTest, PassesColorSourceFrameContextToShader) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    composition->width = 64;
    composition->height = 64;
    composition->duration = 60;
    composition->frameRate = {30, 1};
    composition->backgroundColor = Color{0, 0, 0, 1};

    ShaderDefinition shader;
    shader.name = "TimeFlash";
    // iTime at frame 0 ≈ 0 → black; at frame 30 ≈ 1.0 → red channel saturated.
    shader.mainImage = "vec4 mainImage(vec2 uv) { return vec4(clamp(iTime, 0.0, 1.0), 0.0, 0.0, 1.0); }";
    document.shaders.push_back(shader);

    Layer *layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    layer->outPoint = 60;
    auto *content = static_cast<ShapeContent *>(layer->content.get());
    auto rect = std::make_unique<ShapeRect>();
    rect->position.setStaticValue(Vec2{32, 32});
    rect->size.setStaticValue(Vec2{64, 64});
    content->geometry = std::move(rect);
    auto fill = std::make_unique<FillStyle>();
    ASSERT_TRUE(BindShaderPaint(*fill, document.shaders[0]).hasValue());
    layer->styles.push_back(std::move(fill));

    TgfxVideoFrameSource source;
    VideoExportOptions options;
    options.width = 64;
    options.height = 64;
    options.frameRate = {30, 1};
    options.outputPath = "/tmp/unused.mp4";
    options.range = {0, 60};

    const auto prepared = source.prepare(document, composition->id, options);
    if (!prepared.hasValue()) {
        GTEST_SKIP() << prepared.error();
    }

    auto frame0 = source.renderFrame(0);
    ASSERT_TRUE(frame0.hasValue()) << frame0.error();
    auto frame30 = source.renderFrame(30);
    ASSERT_TRUE(frame30.hasValue()) << frame30.error();

    CVPixelBufferRef buffer0 = static_cast<CVPixelBufferRef>(frame0->platformHandle);
    CVPixelBufferRef buffer30 = static_cast<CVPixelBufferRef>(frame30->platformHandle);
    ASSERT_EQ(CVPixelBufferLockBaseAddress(buffer0, kCVPixelBufferLock_ReadOnly), kCVReturnSuccess);
    ASSERT_EQ(CVPixelBufferLockBaseAddress(buffer30, kCVPixelBufferLock_ReadOnly), kCVReturnSuccess);
    const BgraPixel pixel0 = SampleCorner(buffer0, 32, 32);
    const BgraPixel pixel30 = SampleCorner(buffer30, 32, 32);
    CVPixelBufferUnlockBaseAddress(buffer0, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferUnlockBaseAddress(buffer30, kCVPixelBufferLock_ReadOnly);

    EXPECT_LT(pixel0.r, 40);
    EXPECT_GT(pixel30.r, 200);

    if (frame0->releaseHandle != nullptr) {
        frame0->releaseHandle(frame0->platformHandle);
    }
    if (frame30->releaseHandle != nullptr) {
        frame30->releaseHandle(frame30->platformHandle);
    }
    source.finish();
}
