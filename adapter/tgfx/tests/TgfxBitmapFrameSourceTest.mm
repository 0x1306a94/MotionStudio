#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "MotionStudio/common/Time.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/LayerType.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeRect.h"
#include "TgfxBitmapFrameSource.h"

using motion::Color;
using motion::Composition;
using motion::Document;
using motion::FillStyle;
using motion::Layer;
using motion::LayerType;
using motion::ShapeContent;
using motion::ShapeRect;
using motion::TgfxBitmapFrameSource;
using motion::TimeRange;
using motion::Vec2;

namespace {

struct RgbaPixel {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

RgbaPixel PixelAt(const uint8_t *rgba, int width, size_t rowBytes, int x, int y) {
    const uint8_t *pixel = rgba + static_cast<size_t>(y) * rowBytes + static_cast<size_t>(x) * 4u;
    return {pixel[0], pixel[1], pixel[2], pixel[3]};
}

}  // namespace

TEST(TgfxBitmapFrameSourceTest, CompositionRendersNonEmptyPixels) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    composition->width = 100;
    composition->height = 80;
    composition->duration = 1;
    composition->frameRate = {30, 1};
    composition->backgroundColor = Color{0, 0, 0, 1};
    composition->cornerRadius = 20.0f;

    Layer *layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    layer->outPoint = 1;
    auto *content = static_cast<ShapeContent *>(layer->content.get());
    auto rect = std::make_unique<ShapeRect>();
    rect->position.setStaticValue(Vec2{50, 40});
    rect->size.setStaticValue(Vec2{100, 80});
    content->geometry = std::move(rect);
    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{1, 0, 0, 1});
    layer->styles.push_back(std::move(fill));

    TgfxBitmapFrameSource source;
    TimeRange range;
    range.start = 0;
    range.end = 1;
    const auto prepared =
        source.prepareComposition(document, composition->id, range, composition->width,
                                  composition->height);
    if (!prepared.hasValue()) {
        GTEST_SKIP() << prepared.error();
    }

    auto frame = source.renderFrame(0);
    ASSERT_TRUE(frame.hasValue()) << frame.error();
    EXPECT_EQ(frame->width, 100);
    EXPECT_EQ(frame->height, 80);
    EXPECT_TRUE(frame->premultiplied);
    ASSERT_NE(frame->rgba, nullptr);

    const RgbaPixel center = PixelAt(frame->rgba, frame->width, frame->rowBytes, 50, 40);
    EXPECT_GT(center.r, 200);
    EXPECT_LT(center.g, 40);
    EXPECT_LT(center.b, 40);
    EXPECT_GT(center.a, 200);

    // cornerRadius must be ignored: opaque red reaches corners of the rect.
    const RgbaPixel topLeft = PixelAt(frame->rgba, frame->width, frame->rowBytes, 1, 1);
    EXPECT_GT(topLeft.r, 200);
    EXPECT_GT(topLeft.a, 200);

    source.finish();
}

TEST(TgfxBitmapFrameSourceTest, LayerPrepareExcludesSiblingColor) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    composition->width = 100;
    composition->height = 100;
    composition->duration = 1;
    composition->frameRate = {30, 1};
    composition->backgroundColor = Color{0, 1, 0, 1};

    Layer *layerA = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    layerA->outPoint = 1;
    {
        auto *content = static_cast<ShapeContent *>(layerA->content.get());
        auto rect = std::make_unique<ShapeRect>();
        rect->position.setStaticValue(Vec2{25, 50});
        rect->size.setStaticValue(Vec2{50, 100});
        content->geometry = std::move(rect);
        auto fill = std::make_unique<FillStyle>();
        fill->color.setStaticValue(Color{1, 0, 0, 1});
        layerA->styles.push_back(std::move(fill));
    }

    Layer *layerB = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    layerB->outPoint = 1;
    {
        auto *content = static_cast<ShapeContent *>(layerB->content.get());
        auto rect = std::make_unique<ShapeRect>();
        rect->position.setStaticValue(Vec2{75, 50});
        rect->size.setStaticValue(Vec2{50, 100});
        content->geometry = std::move(rect);
        auto fill = std::make_unique<FillStyle>();
        fill->color.setStaticValue(Color{0, 0, 1, 1});
        layerB->styles.push_back(std::move(fill));
    }

    TgfxBitmapFrameSource source;
    TimeRange range;
    range.start = 0;
    range.end = 1;
    const auto prepared = source.prepare(document, composition->id, layerA->id, range,
                                         composition->width, composition->height);
    if (!prepared.hasValue()) {
        GTEST_SKIP() << prepared.error();
    }

    auto frame = source.renderFrame(0);
    ASSERT_TRUE(frame.hasValue()) << frame.error();
    EXPECT_EQ(frame->width, 100);
    EXPECT_EQ(frame->height, 100);

    const RgbaPixel left = PixelAt(frame->rgba, frame->width, frame->rowBytes, 25, 50);
    const RgbaPixel right = PixelAt(frame->rgba, frame->width, frame->rowBytes, 75, 50);

    EXPECT_GT(left.r, 200);
    EXPECT_LT(left.b, 40);
    EXPECT_GT(left.a, 200);

    // Sibling blue must not appear; transparent background instead of host green.
    EXPECT_LT(right.b, 40);
    EXPECT_LT(right.g, 40);
    EXPECT_LT(right.a, 40);

    source.finish();
}

TEST(TgfxBitmapFrameSourceTest, HonorsPreparedPixelSizeForMaxResolutionCase) {
    // Reproduces 1920x1080 + maxResolution=720 → 1280x720 without float ceil drift.
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    composition->width = 1920;
    composition->height = 1080;
    composition->duration = 1;
    composition->frameRate = {30, 1};
    composition->backgroundColor = Color{0, 0, 0, 1};

    Layer *layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    layer->outPoint = 1;
    auto *content = static_cast<ShapeContent *>(layer->content.get());
    auto rect = std::make_unique<ShapeRect>();
    rect->position.setStaticValue(Vec2{960, 540});
    rect->size.setStaticValue(Vec2{100, 100});
    content->geometry = std::move(rect);
    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{1, 0, 0, 1});
    layer->styles.push_back(std::move(fill));

    constexpr int kPixelWidth = 1280;
    constexpr int kPixelHeight = 720;
    TgfxBitmapFrameSource source;
    TimeRange range;
    range.start = 0;
    range.end = 1;
    const auto prepared =
        source.prepare(document, composition->id, layer->id, range, kPixelWidth, kPixelHeight);
    if (!prepared.hasValue()) {
        GTEST_SKIP() << prepared.error();
    }

    auto frame = source.renderFrame(0);
    ASSERT_TRUE(frame.hasValue()) << frame.error();
    EXPECT_EQ(frame->width, kPixelWidth);
    EXPECT_EQ(frame->height, kPixelHeight);
    source.finish();
}
