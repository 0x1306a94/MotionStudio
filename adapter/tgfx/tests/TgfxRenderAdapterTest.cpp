#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeFill.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/ShapeStroke.h"
#include "MotionStudio/render/CommandBuilder.h"
#include "MotionStudio/render/SceneEvaluator.h"

#include "TgfxRenderAdapter.h"

using motion::BezierPath;
using motion::BuildCommands;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::EvaluatedLayer;
using motion::EvaluatedShapeItem;
using motion::Layer;
using motion::LayerType;
using motion::LineCap;
using motion::Paint;
using motion::PlayCommands;
using motion::SceneEvaluator;
using motion::SceneState;
using motion::ShapeContent;
using motion::ShapeFill;
using motion::ShapeRect;
using motion::ShapeStroke;
using motion::TgfxRenderAdapter;
using motion::Vec2;

namespace {

struct Pixel {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

Pixel PixelAt(const std::vector<uint8_t> &pixels, int width, int x, int y) {
    const size_t offset = (size_t(y) * size_t(width) + size_t(x)) * 4;
    return {pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]};
}

BezierPath MakeRectPath(float left, float top, float right, float bottom) {
    BezierPath path;
    path.closed = true;
    path.vertices.push_back({{left, top}, {}, {}});
    path.vertices.push_back({{right, top}, {}, {}});
    path.vertices.push_back({{right, bottom}, {}, {}});
    path.vertices.push_back({{left, bottom}, {}, {}});
    return path;
}

}  // namespace

TEST(TgfxRenderAdapterTest, FillsRectOverBackground) {
    auto adapter = TgfxRenderAdapter::Make(100, 100);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    SceneState state;
    state.viewportWidth = 100;
    state.viewportHeight = 100;
    state.backgroundColor = Color{0, 0, 0, 1};
    EvaluatedLayer layer;
    EvaluatedShapeItem item;
    item.path = MakeRectPath(30, 30, 70, 70);
    item.paint = Paint{Color{1, 0, 0, 1}};
    layer.shapeItems.push_back(item);
    state.layers.push_back(std::move(layer));

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const Pixel center = PixelAt(pixels, 100, 50, 50);
    EXPECT_NEAR(center.r, 255, 8);
    EXPECT_NEAR(center.g, 0, 8);
    EXPECT_NEAR(center.b, 0, 8);
    EXPECT_NEAR(center.a, 255, 8);

    const Pixel corner = PixelAt(pixels, 100, 5, 5);
    EXPECT_NEAR(corner.r, 0, 8);
    EXPECT_NEAR(corner.g, 0, 8);
    EXPECT_NEAR(corner.b, 0, 8);
    EXPECT_NEAR(corner.a, 255, 8);
}

TEST(TgfxRenderAdapterTest, LayerOpacityBlendsWithBackground) {
    auto adapter = TgfxRenderAdapter::Make(100, 100);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    SceneState state;
    state.backgroundColor = Color{0, 0, 0, 1};
    EvaluatedLayer layer;
    layer.opacity = 0.5f;
    EvaluatedShapeItem item;
    item.path = MakeRectPath(20, 20, 80, 80);
    item.paint = Paint{Color{0, 1, 0, 1}};
    layer.shapeItems.push_back(item);
    state.layers.push_back(std::move(layer));

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const Pixel center = PixelAt(pixels, 100, 50, 50);
    EXPECT_NEAR(center.g, 128, 8);
    EXPECT_NEAR(center.a, 255, 8);
}

TEST(TgfxRenderAdapterTest, StrokeDrawsAlongPath) {
    auto adapter = TgfxRenderAdapter::Make(100, 100);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    SceneState state;
    state.backgroundColor = Color{1, 1, 1, 1};
    EvaluatedLayer layer;
    EvaluatedShapeItem item;
    item.isStroke = true;
    item.path.vertices.push_back({{20, 50}, {}, {}});
    item.path.vertices.push_back({{80, 50}, {}, {}});
    item.paint = Paint{Color{0, 0, 1, 1}};
    item.strokeWidth = 6;
    item.cap = LineCap::Butt;
    layer.shapeItems.push_back(item);
    state.layers.push_back(std::move(layer));

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const Pixel onLine = PixelAt(pixels, 100, 50, 50);
    EXPECT_NEAR(onLine.b, 255, 8);
    EXPECT_NEAR(onLine.r, 0, 8);
    const Pixel offLine = PixelAt(pixels, 100, 50, 20);
    EXPECT_NEAR(offLine.r, 255, 8);
    EXPECT_NEAR(offLine.b, 255, 8);
}

TEST(TgfxRenderAdapterTest, RendersDocumentPipelineEndToEnd) {
    auto adapter = TgfxRenderAdapter::Make(100, 100);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    composition->width = 100;
    composition->height = 100;
    composition->backgroundColor = Color{0, 0, 0, 1};
    composition->duration = 100;
    Layer *layer =
        document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    layer->outPoint = 100;
    auto *content = static_cast<ShapeContent *>(layer->content.get());
    auto rect = std::make_unique<ShapeRect>();
    rect->position.setStaticValue(Vec2{50, 50});
    rect->size.setStaticValue(Vec2{40, 40});
    content->elements.push_back(std::move(rect));
    auto fill = std::make_unique<ShapeFill>();
    fill->color.setStaticValue(Color{1, 1, 0, 1});
    content->elements.push_back(std::move(fill));

    auto state = SceneEvaluator::Evaluate(document, composition->id, 10);
    ASSERT_TRUE(state.hasValue());

    adapter->beginFrame(state->viewportWidth, state->viewportHeight,
                        state->backgroundColor, state->cornerRadius);
    PlayCommands(BuildCommands(*state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const Pixel center = PixelAt(pixels, 100, 50, 50);
    EXPECT_NEAR(center.r, 255, 8);
    EXPECT_NEAR(center.g, 255, 8);
    EXPECT_NEAR(center.b, 0, 8);
}
