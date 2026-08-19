#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/VectorNetworkConvert.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/GradientType.h"
#include "MotionStudio/model/LayerEffect.h"
#include "MotionStudio/model/LayerFx.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/MaskMode.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/StrokeMode.h"
#include "MotionStudio/model/StylePaintMode.h"
#include "MotionStudio/model/TextAlign.h"
#include "MotionStudio/model/TextContent.h"
#include "MotionStudio/model/TrackMatteType.h"
#include "MotionStudio/render/CommandBuilder.h"
#include "MotionStudio/render/EvaluatedImageItem.h"
#include "MotionStudio/render/EvaluatedMask.h"
#include "MotionStudio/render/EvaluatedTextItem.h"
#include "MotionStudio/render/Paint.h"
#include "MotionStudio/render/SceneEvaluator.h"
#include "MotionStudio/render/ShapeGeometry.h"

#include "TgfxRenderAdapter.h"

using motion::BezierPath;
using motion::BrightnessContrastEffect;
using motion::BuildCommands;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::DrawCommand;
using motion::DrawCommandList;
using motion::DrawCommandType;
using motion::DropShadowStyle;
using motion::EntityId;
using motion::EvaluatedGradientStop;
using motion::EvaluatedLayer;
using motion::EvaluatedMask;
using motion::EvaluatedShapeItem;
using motion::FillStyle;
using motion::GaussianBlurEffect;
using motion::GradientType;
using motion::Layer;
using motion::LayerStrokeStyle;
using motion::LayerType;
using motion::LineCap;
using motion::MakePathGeometry;
using motion::MakeRectGeometry;
using motion::MakeSingleContour;
using motion::MaskMode;
using motion::Mat3;
using motion::OuterGlowStyle;
using motion::Paint;
using motion::PlayCommands;
using motion::SceneEvaluator;
using motion::SceneState;
using motion::ShapeContent;
using motion::ShapeRect;
using motion::StrokeMode;
using motion::StylePaintMode;
using motion::TgfxRenderAdapter;
using motion::TrackMatteType;
using motion::Vec2;

namespace {

struct Pixel {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

Pixel PixelAt(const std::vector<uint8_t> &pixels, int width, int x, int y) {
    const size_t offset =
        (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4;
    return {pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]};
}

std::string RedFixturePath() {
    return (std::filesystem::path(__FILE__).parent_path() / "fixtures" / "red_2x2.png").string();
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
    item.geometry = MakeRectGeometry(Vec2{50, 50}, Vec2{40, 40});
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
    item.geometry = MakeRectGeometry(Vec2{50, 50}, Vec2{60, 60});
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
    BezierPath path = MakeSingleContour({{{20, 50}, {}, {}}, {{80, 50}, {}, {}}}, false);
    item.geometry = MakePathGeometry(std::move(path));
    item.paint = Paint{Color{0, 0, 1, 1}};
    item.stroke.width = 6;
    item.stroke.cap = LineCap::Butt;
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
    content->geometry = std::move(rect);
    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{1, 1, 0, 1});
    layer->styles.push_back(std::move(fill));

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

TEST(TgfxRenderAdapterTest, PathMaskAddClipsLayerContent) {
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
    item.geometry = MakeRectGeometry(Vec2{50, 50}, Vec2{80, 80});
    item.paint = Paint{Color{1, 0, 0, 1}};
    layer.shapeItems.push_back(item);
    EvaluatedMask mask;
    mask.mode = MaskMode::Add;
    // 20x20 rect centered at (50,50) in path form via MakeRectGeometry converted...
    // Use an explicit closed path covering [40,60] x [40,60].
    BezierPath path = MakeSingleContour({{{40, 40}, {}, {}}, {{60, 40}, {}, {}}, {{60, 60}, {}, {}}, {{40, 60}, {}, {}}}, true);
    mask.path = path;
    layer.masks.push_back(mask);
    state.layers.push_back(std::move(layer));

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const Pixel inside = PixelAt(pixels, 100, 50, 50);
    EXPECT_NEAR(inside.r, 255, 8);
    const Pixel outside = PixelAt(pixels, 100, 20, 20);
    EXPECT_NEAR(outside.r, 0, 8);
}

// Inv must use content bounds as the coverage clip; path-only bounds make inverse
// fill empty (clip == path) so Inv appears to do nothing.
TEST(TgfxRenderAdapterTest, PathMaskInvertedRevealsOutsidePath) {
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
    item.geometry = MakeRectGeometry(Vec2{50, 50}, Vec2{80, 80});
    item.paint = Paint{Color{1, 0, 0, 1}};
    layer.shapeItems.push_back(item);
    EvaluatedMask mask;
    mask.mode = MaskMode::Add;
    mask.inverted = true;
    BezierPath path = MakeSingleContour(
        {{{40, 40}, {}, {}}, {{60, 40}, {}, {}}, {{60, 60}, {}, {}}, {{40, 60}, {}, {}}}, true);
    mask.path = path;
    layer.masks.push_back(mask);
    state.layers.push_back(std::move(layer));

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const Pixel insideMask = PixelAt(pixels, 100, 50, 50);
    EXPECT_NEAR(insideMask.r, 0, 8);
    const Pixel outsideMaskInsideContent = PixelAt(pixels, 100, 20, 50);
    EXPECT_NEAR(outsideMaskInsideContent.r, 255, 8);
}

TEST(TgfxRenderAdapterTest, PathMaskFeatherSoftensEdge) {
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
    item.geometry = MakeRectGeometry(Vec2{50, 50}, Vec2{80, 80});
    item.paint = Paint{Color{1, 0, 0, 1}};
    layer.shapeItems.push_back(item);
    EvaluatedMask mask;
    mask.mode = MaskMode::Add;
    mask.feather = 8.0f;
    BezierPath path = MakeSingleContour({{{40, 40}, {}, {}}, {{60, 40}, {}, {}}, {{60, 60}, {}, {}}, {{40, 60}, {}, {}}}, true);
    mask.path = path;
    layer.masks.push_back(mask);
    state.layers.push_back(std::move(layer));

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const Pixel nearEdge = PixelAt(pixels, 100, 36, 50);
    EXPECT_GT(nearEdge.r, 8);
    EXPECT_LT(nearEdge.r, 250);
}

TEST(TgfxRenderAdapterTest, AlphaTrackMatteMasksTargetLayer) {
    auto adapter = TgfxRenderAdapter::Make(100, 100);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    SceneState state;
    state.viewportWidth = 100;
    state.viewportHeight = 100;
    state.backgroundColor = Color{0, 0, 0, 1};

    EvaluatedLayer matte;
    matte.id = EntityId{1};
    matte.usedAsMatteOnly = true;
    matte.worldTransform = Mat3::Identity();
    EvaluatedShapeItem matteItem;
    matteItem.geometry = MakeRectGeometry(Vec2{50, 50}, Vec2{20, 20});
    matteItem.paint = Paint{Color{1, 1, 1, 1}};
    matte.shapeItems.push_back(matteItem);

    EvaluatedLayer target;
    target.id = EntityId{2};
    target.worldTransform = Mat3::Identity();
    target.trackMatteType = TrackMatteType::Alpha;
    target.matteSourceId = EntityId{1};
    EvaluatedShapeItem targetItem;
    targetItem.geometry = MakeRectGeometry(Vec2{50, 50}, Vec2{80, 80});
    targetItem.paint = Paint{Color{0, 0, 1, 1}};
    target.shapeItems.push_back(targetItem);

    state.layers.push_back(std::move(matte));
    state.layers.push_back(std::move(target));

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const Pixel inside = PixelAt(pixels, 100, 50, 50);
    EXPECT_NEAR(inside.b, 255, 8);
    const Pixel outside = PixelAt(pixels, 100, 20, 20);
    EXPECT_NEAR(outside.b, 0, 8);
}

TEST(TgfxRenderAdapterTest, StrokeTrimKeepsOnlyPartialSegment) {
    auto adapter = TgfxRenderAdapter::Make(100, 100);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    SceneState state;
    state.backgroundColor = Color{1, 1, 1, 1};
    EvaluatedLayer layer;
    EvaluatedShapeItem item;
    item.isStroke = true;
    BezierPath path = MakeSingleContour({{{20, 50}, {}, {}}, {{80, 50}, {}, {}}}, false);
    item.geometry = MakePathGeometry(std::move(path));
    item.paint = Paint{Color{0, 0, 1, 1}};
    item.stroke.width = 6;
    item.stroke.cap = LineCap::Butt;
    item.stroke.trimStart = 0.5f;
    item.stroke.trimEnd = 1.0f;
    layer.shapeItems.push_back(item);
    state.layers.push_back(std::move(layer));

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    // First half of the segment is trimmed away.
    const Pixel trimmedAway = PixelAt(pixels, 100, 30, 50);
    EXPECT_NEAR(trimmedAway.r, 255, 8);
    EXPECT_NEAR(trimmedAway.b, 255, 8);
    // Second half remains stroked.
    const Pixel kept = PixelAt(pixels, 100, 65, 50);
    EXPECT_NEAR(kept.b, 255, 8);
    EXPECT_NEAR(kept.r, 0, 8);
}

TEST(TgfxRenderAdapterTest, StrokeDashLeavesGapOnLongInterval) {
    auto adapter = TgfxRenderAdapter::Make(100, 100);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    SceneState state;
    state.backgroundColor = Color{1, 1, 1, 1};
    EvaluatedLayer layer;
    EvaluatedShapeItem item;
    item.isStroke = true;
    BezierPath path = MakeSingleContour({{{20, 50}, {}, {}}, {{80, 50}, {}, {}}}, false);
    item.geometry = MakePathGeometry(std::move(path));
    item.paint = Paint{Color{0, 0, 1, 1}};
    item.stroke.width = 6;
    item.stroke.cap = LineCap::Butt;
    item.stroke.strokeMode = StrokeMode::Dashed;
    item.stroke.dashes = {10.0f, 50.0f};
    item.stroke.dashOffset = 0.0f;
    layer.shapeItems.push_back(item);
    state.layers.push_back(std::move(layer));

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const Pixel onDash = PixelAt(pixels, 100, 25, 50);
    EXPECT_NEAR(onDash.b, 255, 8);
    EXPECT_NEAR(onDash.r, 0, 8);
    const Pixel inGap = PixelAt(pixels, 100, 50, 50);
    EXPECT_NEAR(inGap.r, 255, 8);
    EXPECT_NEAR(inGap.b, 255, 8);
}

TEST(TgfxRenderAdapterTest, StrokeDashSolidIgnoresStoredPattern) {
    auto adapter = TgfxRenderAdapter::Make(100, 100);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    SceneState state;
    state.backgroundColor = Color{1, 1, 1, 1};
    EvaluatedLayer layer;
    EvaluatedShapeItem item;
    item.isStroke = true;
    BezierPath path = MakeSingleContour({{{20, 50}, {}, {}}, {{80, 50}, {}, {}}}, false);
    item.geometry = MakePathGeometry(std::move(path));
    item.paint = Paint{Color{0, 0, 1, 1}};
    item.stroke.width = 6;
    item.stroke.cap = LineCap::Butt;
    item.stroke.strokeMode = StrokeMode::Solid;
    item.stroke.dashes = {10.0f, 50.0f};
    layer.shapeItems.push_back(item);
    state.layers.push_back(std::move(layer));

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const Pixel mid = PixelAt(pixels, 100, 50, 50);
    EXPECT_NEAR(mid.b, 255, 8);
    EXPECT_NEAR(mid.r, 0, 8);
}

TEST(TgfxRenderAdapterTest, StrokeEmptyTrimDrawsNothing) {
    auto adapter = TgfxRenderAdapter::Make(100, 100);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    SceneState state;
    state.backgroundColor = Color{1, 1, 1, 1};
    EvaluatedLayer layer;
    EvaluatedShapeItem item;
    item.isStroke = true;
    BezierPath path = MakeSingleContour({{{20, 50}, {}, {}}, {{80, 50}, {}, {}}}, false);
    item.geometry = MakePathGeometry(std::move(path));
    item.paint = Paint{Color{0, 0, 1, 1}};
    item.stroke.width = 6;
    item.stroke.trimStart = 0.4f;
    item.stroke.trimEnd = 0.4f;
    layer.shapeItems.push_back(item);
    state.layers.push_back(std::move(layer));

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const Pixel center = PixelAt(pixels, 100, 50, 50);
    EXPECT_NEAR(center.r, 255, 8);
    EXPECT_NEAR(center.b, 255, 8);
}

TEST(TgfxRenderAdapterTest, DrawsStretchedImageIntoContainer) {
    auto adapter = TgfxRenderAdapter::Make(100, 100);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    const std::string path = RedFixturePath();
    ASSERT_TRUE(std::filesystem::exists(path)) << path;

    SceneState state;
    state.viewportWidth = 100;
    state.viewportHeight = 100;
    state.backgroundColor = Color{0, 0, 0, 1};
    EvaluatedLayer layer;
    motion::EvaluatedImageItem image;
    image.absolutePath = path;
    image.containerSize = {100, 100};
    image.intrinsicSize = {2, 2};
    image.scaleMode = motion::ImageScaleMode::Stretch;
    layer.imageItem = std::move(image);
    state.layers.push_back(std::move(layer));

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const Pixel center = PixelAt(pixels, 100, 50, 50);
    EXPECT_NEAR(center.r, 255, 20);
    EXPECT_NEAR(center.g, 0, 20);
    EXPECT_NEAR(center.b, 0, 20);
}

TEST(TgfxRenderAdapterTest, LetterBoxLeavesSideMargins) {
    auto adapter = TgfxRenderAdapter::Make(100, 50);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    const std::string path = RedFixturePath();
    ASSERT_TRUE(std::filesystem::exists(path)) << path;

    SceneState state;
    state.viewportWidth = 100;
    state.viewportHeight = 50;
    state.backgroundColor = Color{0, 0, 0, 1};
    EvaluatedLayer layer;
    motion::EvaluatedImageItem image;
    image.absolutePath = path;
    image.containerSize = {100, 50};
    image.intrinsicSize = {2, 2};
    image.scaleMode = motion::ImageScaleMode::LetterBox;
    layer.imageItem = std::move(image);
    state.layers.push_back(std::move(layer));

    adapter->beginFrame(100, 50, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const Pixel center = PixelAt(pixels, 100, 50, 25);
    EXPECT_NEAR(center.r, 255, 20);
    const Pixel leftMargin = PixelAt(pixels, 100, 5, 25);
    EXPECT_NEAR(leftMargin.r, 0, 20);
    EXPECT_NEAR(leftMargin.g, 0, 20);
    EXPECT_NEAR(leftMargin.b, 0, 20);
}

TEST(TgfxRenderAdapterTest, DrawsTextOverBackground) {
    auto adapter = TgfxRenderAdapter::Make(200, 120);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    SceneState state;
    state.viewportWidth = 200;
    state.viewportHeight = 120;
    state.backgroundColor = Color{1, 1, 1, 1};
    EvaluatedLayer layer;
    motion::EvaluatedTextItem text;
    text.text = "Hi";
    text.fontSize = 64.0f;
    text.containerSize = {200, 120};
    text.boxTextMode = false;
    text.align = motion::TextAlign::Left;
    text.fontFamily = "Helvetica";
    motion::TextDrawStyle fill;
    fill.color = Color{0, 0, 0, 1};
    text.styles = {fill};
    layer.textItem = std::move(text);
    state.layers.push_back(std::move(layer));

    adapter->beginFrame(200, 120, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));

    bool foundInk = false;
    for (int y = 0; y < 120 && !foundInk; ++y) {
        for (int x = 0; x < 200; ++x) {
            const Pixel pixel = PixelAt(pixels, 200, x, y);
            if (pixel.r < 240 || pixel.g < 240 || pixel.b < 240) {
                foundInk = true;
                break;
            }
        }
    }
    EXPECT_TRUE(foundInk);
}

TEST(TgfxRenderAdapterTest, DrawsTextOnPathAndCachesLayout) {
    auto adapter = TgfxRenderAdapter::Make(200, 120);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    BezierPath path = MakeSingleContour({{{10.0f, 60.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}}, {{190.0f, 60.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}}}, false);
    SceneState state;
    state.viewportWidth = 200;
    state.viewportHeight = 120;
    state.backgroundColor = Color{1, 1, 1, 1};
    EvaluatedLayer layer;
    motion::EvaluatedTextItem text;
    text.text = "AB";
    text.fontSize = 32.0f;
    text.boxTextMode = false;
    text.align = motion::TextAlign::Left;
    text.fontFamily = "Helvetica";
    motion::TextDrawStyle fill;
    fill.color = Color{0, 0, 0, 1};
    text.styles = {fill};
    motion::EvaluatedTextPath textPath;
    textPath.path = path;
    textPath.perpendicular = true;
    text.textPath = std::move(textPath);
    layer.textItem = std::move(text);
    state.layers.push_back(std::move(layer));

    const auto commands = BuildCommands(state);
    adapter->beginFrame(200, 120, state.backgroundColor, state.cornerRadius);
    PlayCommands(commands, *adapter);
    EXPECT_EQ(adapter->textPathCacheHitsForTest(), 0u);
    PlayCommands(commands, *adapter);
    EXPECT_EQ(adapter->textPathCacheHitsForTest(), 1u);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    bool foundInk = false;
    for (int y = 0; y < 120 && !foundInk; ++y) {
        for (int x = 0; x < 200; ++x) {
            const Pixel pixel = PixelAt(pixels, 200, x, y);
            if (pixel.r < 240 || pixel.g < 240 || pixel.b < 240) {
                foundInk = true;
                break;
            }
        }
    }
    EXPECT_TRUE(foundInk);
}

TEST(TgfxRenderAdapterTest, TextOnPathBaselineAlignsWithPathY) {
    auto adapter = TgfxRenderAdapter::Make(200, 120);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    constexpr float kPathY = 60.0f;
    BezierPath path = MakeSingleContour({{{10.0f, kPathY}, {0.0f, 0.0f}, {0.0f, 0.0f}}, {{190.0f, kPathY}, {0.0f, 0.0f}, {0.0f, 0.0f}}}, false);
    SceneState state;
    state.viewportWidth = 200;
    state.viewportHeight = 120;
    state.backgroundColor = Color{1, 1, 1, 1};
    EvaluatedLayer layer;
    motion::EvaluatedTextItem text;
    text.text = "Hello";
    text.fontSize = 32.0f;
    text.boxTextMode = false;
    text.align = motion::TextAlign::Left;
    text.fontFamily = "Helvetica";
    motion::TextDrawStyle fill;
    fill.color = Color{0, 0, 0, 1};
    text.styles = {fill};
    motion::EvaluatedTextPath textPath;
    textPath.path = path;
    textPath.perpendicular = true;
    text.textPath = std::move(textPath);
    layer.textItem = std::move(text);
    state.layers.push_back(std::move(layer));

    adapter->beginFrame(200, 120, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));

    int inkTop = 120;
    int inkBottom = -1;
    for (int y = 0; y < 120; ++y) {
        for (int x = 0; x < 200; ++x) {
            const Pixel pixel = PixelAt(pixels, 200, x, y);
            if (pixel.r < 240 || pixel.g < 240 || pixel.b < 240) {
                inkTop = std::min(inkTop, y);
                inkBottom = std::max(inkBottom, y);
            }
        }
    }
    ASSERT_GE(inkBottom, 0);
    const float mid = 0.5f * static_cast<float>(inkTop + inkBottom);
    // Baseline-on-path: pathY near inkBottom (caps), not vertical mid.
    EXPECT_LT(std::fabs(kPathY - static_cast<float>(inkBottom)), std::fabs(kPathY - mid));
    EXPECT_NEAR(kPathY, static_cast<float>(inkBottom), 8.0f);
}

TEST(TgfxRenderAdapterTest, SceneTextPathBaselineAlignsWithPathStroke) {
    auto adapter = TgfxRenderAdapter::Make(200, 120);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    composition->width = 200;
    composition->height = 120;
    composition->duration = 100;
    composition->backgroundColor = Color{1, 1, 1, 1};

    Layer *pathLayer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    pathLayer->outPoint = 100;
    pathLayer->transform.position.setStaticValue({0.0f, 60.0f});
    auto *pathContent = static_cast<ShapeContent *>(pathLayer->content.get());
    auto pathElement = std::make_unique<motion::ShapePath>();
    BezierPath path = MakeSingleContour({{{10.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}}, {{190.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}}}, false);
    pathElement->path.setStaticValue(motion::BezierPathToVectorNetwork(path));
    pathContent->geometry = std::move(pathElement);
    auto stroke = std::make_unique<motion::StrokeStyle>();
    stroke->color.setStaticValue(Color{0.0f, 0.0f, 1.0f, 1.0f});  // blue path
    stroke->width.setStaticValue(2.0f);
    pathLayer->styles.push_back(std::move(stroke));

    Layer *textLayer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Text));
    textLayer->outPoint = 100;
    auto *textContent = static_cast<motion::TextContent *>(textLayer->content.get());
    textContent->text.setStaticValue("Hello");
    textContent->fontSize = 32.0f;
    textContent->fontFamily = "Helvetica";
    textContent->textPath.enabled = true;
    textContent->textPath.pathLayerId = pathLayer->id;
    textContent->textPath.perpendicular = true;
    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{0.0f, 0.0f, 0.0f, 1.0f});  // black text
    textLayer->styles.push_back(std::move(fill));
    document.refreshEntityIndex();

    auto state = SceneEvaluator::Evaluate(document, composition->id, 0);
    ASSERT_TRUE(state.hasValue()) << state.error();

    adapter->beginFrame(200, 120, state->backgroundColor, state->cornerRadius);
    PlayCommands(BuildCommands(*state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));

    // Find black text ink (not blue path): r=g=b and dark.
    int textTop = 120;
    int textBottom = -1;
    for (int y = 0; y < 120; ++y) {
        for (int x = 15; x < 120; ++x) {
            const Pixel pixel = PixelAt(pixels, 200, x, y);
            if (pixel.r < 80 && pixel.g < 80 && pixel.b < 80) {
                textTop = std::min(textTop, y);
                textBottom = std::max(textBottom, y);
            }
        }
    }
    ASSERT_GE(textBottom, 0) << "no black text ink";
    const float mid = 0.5f * static_cast<float>(textTop + textBottom);
    std::printf("scene textTop=%d textBottom=%d mid=%.1f pathY=60\n", textTop, textBottom, mid);
    EXPECT_LT(std::fabs(60.0f - static_cast<float>(textBottom)), std::fabs(60.0f - mid));
    EXPECT_NEAR(60.0f, static_cast<float>(textBottom), 10.0f);
}

TEST(TgfxRenderAdapterTest, LinearGradientFillDraws) {
    auto adapter = TgfxRenderAdapter::Make(64, 64);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    SceneState state;
    state.viewportWidth = 64;
    state.viewportHeight = 64;
    state.backgroundColor = Color{0, 0, 0, 1};

    EvaluatedLayer layer;
    layer.id = EntityId{1};
    layer.worldTransform = Mat3::Identity();
    EvaluatedShapeItem item;
    item.geometry = MakeRectGeometry(Vec2{32, 32}, Vec2{40, 40});
    item.paint.paintMode = StylePaintMode::Gradient;
    item.paint.gradient.type = GradientType::Linear;
    item.paint.gradient.start = Vec2{12, 32};
    item.paint.gradient.end = Vec2{52, 32};
    item.paint.gradient.stops = {
        EvaluatedGradientStop{Color{1, 0, 0, 1}, 0.f},
        EvaluatedGradientStop{Color{0, 0, 1, 1}, 1.f},
    };
    layer.shapeItems.push_back(item);
    state.layers.push_back(std::move(layer));

    adapter->beginFrame(64, 64, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const Pixel left = PixelAt(pixels, 64, 18, 32);
    const Pixel right = PixelAt(pixels, 64, 46, 32);
    EXPECT_GT(left.r, right.r);
    EXPECT_GT(right.b, left.b);
}

TEST(TgfxRenderAdapterTest, ShaderFillDrawsUvGradient) {
    auto adapter = TgfxRenderAdapter::Make(64, 64);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    SceneState state;
    state.viewportWidth = 64;
    state.viewportHeight = 64;
    state.backgroundColor = Color{0, 0, 0, 1};
    state.frameRate = 30.f;
    state.frameIndex = 0;
    state.timeSeconds = 0.f;

    EvaluatedLayer layer;
    layer.id = EntityId{1};
    layer.worldTransform = Mat3::Identity();
    EvaluatedShapeItem item;
    item.geometry = MakeRectGeometry(Vec2{32, 32}, Vec2{40, 40});
    item.paint.paintMode = StylePaintMode::Shader;
    item.paint.shader.shaderId = EntityId::Generate();
    item.paint.shader.mainImage = "vec4 mainImage(vec2 uv) { return vec4(uv, 0.0, 1.0); }";
    layer.shapeItems.push_back(item);
    state.layers.push_back(std::move(layer));

    adapter->setColorSourceFrameContext(state.timeSeconds, state.frameIndex, state.frameRate);
    adapter->beginFrame(64, 64, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    // Center of the rect should be roughly mid UV (not pure black / pure white).
    const Pixel center = PixelAt(pixels, 64, 32, 32);
    EXPECT_GT(center.r, 20);
    EXPECT_GT(center.g, 20);
    EXPECT_LT(center.r, 250);
    EXPECT_LT(center.g, 250);
    // Bottom-right of the fill should be greener/redder than top-left.
    const Pixel topLeft = PixelAt(pixels, 64, 18, 18);
    const Pixel bottomRight = PixelAt(pixels, 64, 46, 46);
    EXPECT_GT(bottomRight.r, topLeft.r);
    EXPECT_GT(bottomRight.g, topLeft.g);
}

TEST(TgfxRenderAdapterTest, BadShaderSourceSkipsFill) {
    auto adapter = TgfxRenderAdapter::Make(32, 32);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    SceneState state;
    state.viewportWidth = 32;
    state.viewportHeight = 32;
    state.backgroundColor = Color{0, 0, 0, 1};

    EvaluatedLayer layer;
    layer.worldTransform = Mat3::Identity();
    EvaluatedShapeItem item;
    item.geometry = MakeRectGeometry(Vec2{16, 16}, Vec2{20, 20});
    item.paint.paintMode = StylePaintMode::Shader;
    item.paint.shader.shaderId = EntityId::Generate();
    item.paint.shader.mainImage = "this is not valid glsl {{{";
    layer.shapeItems.push_back(item);
    state.layers.push_back(std::move(layer));

    adapter->beginFrame(32, 32, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const Pixel center = PixelAt(pixels, 32, 16, 16);
    EXPECT_NEAR(center.r, 0, 8);
    EXPECT_NEAR(center.g, 0, 8);
    EXPECT_NEAR(center.b, 0, 8);
}

TEST(TgfxRenderAdapterTest, BrightnessContrastRaisesCenterLuma) {
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
    item.geometry = MakeRectGeometry(Vec2{50, 50}, Vec2{40, 40});
    item.paint = Paint{Color{0.5f, 0.5f, 0.5f, 1}};
    layer.shapeItems.push_back(item);
    state.layers.push_back(layer);

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();
    std::vector<uint8_t> plainPixels;
    ASSERT_TRUE(adapter->ReadPixels(plainPixels));
    const Pixel plain = PixelAt(plainPixels, 100, 50, 50);
    const int plainLuma = static_cast<int>(plain.r) + static_cast<int>(plain.g) +
        static_cast<int>(plain.b);

    auto brightnessContrast = std::make_shared<BrightnessContrastEffect>();
    brightnessContrast->brightness.setStaticValue(100.0f);
    state.layers[0].effects.push_back(std::move(brightnessContrast));

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();
    std::vector<uint8_t> filteredPixels;
    ASSERT_TRUE(adapter->ReadPixels(filteredPixels));
    const Pixel filtered = PixelAt(filteredPixels, 100, 50, 50);
    const int filteredLuma = static_cast<int>(filtered.r) + static_cast<int>(filtered.g) +
        static_cast<int>(filtered.b);
    EXPECT_GT(filteredLuma, plainLuma + 20);
}

TEST(TgfxRenderAdapterTest, LayerOpacityAppliesAfterBrightnessContrast) {
    auto adapter = TgfxRenderAdapter::Make(100, 100);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    SceneState state;
    state.viewportWidth = 100;
    state.viewportHeight = 100;
    state.backgroundColor = Color{0, 0, 0, 0};
    EvaluatedLayer layer;
    layer.opacity = 0.5f;
    EvaluatedShapeItem item;
    item.geometry = MakeRectGeometry(Vec2{50, 50}, Vec2{40, 40});
    item.paint = Paint{Color{1, 0, 0, 1}};
    layer.shapeItems.push_back(item);
    auto brightnessContrast = std::make_shared<BrightnessContrastEffect>();
    brightnessContrast->brightness.setStaticValue(10.0f);
    layer.effects.push_back(std::move(brightnessContrast));
    state.layers.push_back(std::move(layer));

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const Pixel center = PixelAt(pixels, 100, 50, 50);
    EXPECT_NEAR(center.a, 128, 20);
}

TEST(TgfxRenderAdapterTest, GaussianBlurSoftensRectEdge) {
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
    item.geometry = MakeRectGeometry(Vec2{50, 50}, Vec2{20, 20});
    item.paint = Paint{Color{1, 1, 1, 1}};
    layer.shapeItems.push_back(item);
    state.layers.push_back(layer);

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();
    std::vector<uint8_t> plainPixels;
    ASSERT_TRUE(adapter->ReadPixels(plainPixels));
    const Pixel plainOutside = PixelAt(plainPixels, 100, 64, 50);
    EXPECT_NEAR(plainOutside.a, 255, 8);
    EXPECT_NEAR(plainOutside.r, 0, 8);

    auto blur = std::make_shared<GaussianBlurEffect>();
    blur->blurriness.setStaticValue(16.0f);
    state.layers[0].effects.push_back(std::move(blur));

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();
    std::vector<uint8_t> blurPixels;
    ASSERT_TRUE(adapter->ReadPixels(blurPixels));
    const Pixel blurOutside = PixelAt(blurPixels, 100, 64, 50);
    EXPECT_GT(blurOutside.r, 8);
}

TEST(TgfxRenderAdapterTest, GaussianBlurKeepsFillColor) {
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
    item.geometry = MakeRectGeometry(Vec2{50, 50}, Vec2{40, 40});
    item.paint = Paint{Color{1, 0, 0, 1}};
    layer.shapeItems.push_back(item);
    auto blur = std::make_shared<GaussianBlurEffect>();
    blur->blurriness.setStaticValue(16.0f);
    layer.effects.push_back(std::move(blur));
    state.layers.push_back(std::move(layer));

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const Pixel center = PixelAt(pixels, 100, 50, 50);
    EXPECT_GT(center.r, 200);
    EXPECT_LT(center.g, 40);
    EXPECT_LT(center.b, 40);
}

TEST(TgfxRenderAdapterTest, EffectOrderChangesHalfBlackHalfWhite) {
    auto adapter = TgfxRenderAdapter::Make(64, 64);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    auto makeHalf = []() {
        SceneState state;
        state.viewportWidth = 64;
        state.viewportHeight = 64;
        state.backgroundColor = Color{0, 0, 0, 1};
        EvaluatedLayer layer;
        EvaluatedShapeItem black;
        black.geometry = MakeRectGeometry(Vec2{16, 32}, Vec2{32, 64});
        black.paint = Paint{Color{0, 0, 0, 1}};
        EvaluatedShapeItem white;
        white.geometry = MakeRectGeometry(Vec2{48, 32}, Vec2{32, 64});
        white.paint = Paint{Color{1, 1, 1, 1}};
        layer.shapeItems.push_back(black);
        layer.shapeItems.push_back(white);
        state.layers.push_back(std::move(layer));
        return state;
    };

    auto brightnessContrast = []() {
        auto effect = std::make_shared<BrightnessContrastEffect>();
        effect->contrast.setStaticValue(80.0f);
        return effect;
    };
    auto blur = []() {
        auto effect = std::make_shared<GaussianBlurEffect>();
        effect->blurriness.setStaticValue(12.0f);
        return effect;
    };

    SceneState first = makeHalf();
    first.layers[0].effects.push_back(brightnessContrast());
    first.layers[0].effects.push_back(blur());
    adapter->beginFrame(64, 64, first.backgroundColor, first.cornerRadius);
    PlayCommands(BuildCommands(first), *adapter);
    adapter->endFrame();
    std::vector<uint8_t> firstPixels;
    ASSERT_TRUE(adapter->ReadPixels(firstPixels));

    SceneState second = makeHalf();
    second.layers[0].effects.push_back(blur());
    second.layers[0].effects.push_back(brightnessContrast());
    adapter->beginFrame(64, 64, second.backgroundColor, second.cornerRadius);
    PlayCommands(BuildCommands(second), *adapter);
    adapter->endFrame();
    std::vector<uint8_t> secondPixels;
    ASSERT_TRUE(adapter->ReadPixels(secondPixels));

    ASSERT_EQ(firstPixels.size(), secondPixels.size());
    EXPECT_NE(firstPixels, secondPixels);
}

TEST(TgfxRenderAdapterTest, BlurBleedsOutsideAddMask) {
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
    item.geometry = MakeRectGeometry(Vec2{50, 50}, Vec2{80, 80});
    item.paint = Paint{Color{1, 1, 1, 1}};
    layer.shapeItems.push_back(item);
    EvaluatedMask mask;
    mask.mode = MaskMode::Add;
    BezierPath path =
        MakeSingleContour({{{40, 40}, {}, {}}, {{60, 40}, {}, {}}, {{60, 60}, {}, {}}, {{40, 60}, {}, {}}},
                          true);
    mask.path = path;
    layer.masks.push_back(mask);
    state.layers.push_back(layer);

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();
    std::vector<uint8_t> plainPixels;
    ASSERT_TRUE(adapter->ReadPixels(plainPixels));
    const Pixel plainBleed = PixelAt(plainPixels, 100, 68, 50);
    EXPECT_NEAR(plainBleed.r, 0, 8);

    auto blur = std::make_shared<GaussianBlurEffect>();
    blur->blurriness.setStaticValue(16.0f);
    state.layers[0].effects.push_back(std::move(blur));

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();
    std::vector<uint8_t> blurPixels;
    ASSERT_TRUE(adapter->ReadPixels(blurPixels));
    const Pixel blurBleed = PixelAt(blurPixels, 100, 68, 50);
    EXPECT_GT(blurBleed.r, 8);
}

namespace {

SceneState MakeCenteredWhiteRect() {
    SceneState state;
    state.viewportWidth = 100;
    state.viewportHeight = 100;
    state.backgroundColor = Color{0, 0, 0, 1};
    EvaluatedLayer layer;
    EvaluatedShapeItem item;
    item.geometry = MakeRectGeometry(Vec2{50, 50}, Vec2{20, 20});
    item.paint = Paint{Color{1, 1, 1, 1}};
    layer.shapeItems.push_back(item);
    state.layers.push_back(std::move(layer));
    return state;
}

}  // namespace

TEST(TgfxRenderAdapterTest, DropShadowKeepsFillAndCastsOffset) {
    auto adapter = TgfxRenderAdapter::Make(100, 100);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    SceneState state = MakeCenteredWhiteRect();
    auto shadow = std::make_shared<DropShadowStyle>();
    shadow->color.setStaticValue(Color{1, 0, 0, 1});
    state.layers[0].layerStyles.push_back(std::move(shadow));

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();
    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const Pixel center = PixelAt(pixels, 100, 50, 50);
    EXPECT_GT(center.r, 200);
    EXPECT_GT(center.g, 200);
    EXPECT_GT(center.b, 200);
    const Pixel offsetPixel = PixelAt(pixels, 100, 64, 64);
    EXPECT_GT(static_cast<int>(offsetPixel.r) + static_cast<int>(offsetPixel.g) +
                  static_cast<int>(offsetPixel.b),
              8);
}

TEST(TgfxRenderAdapterTest, OuterGlowTintsAroundRect) {
    auto adapter = TgfxRenderAdapter::Make(100, 100);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    SceneState state = MakeCenteredWhiteRect();
    state.layers[0].layerStyles.push_back(std::make_shared<OuterGlowStyle>());

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();
    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const Pixel center = PixelAt(pixels, 100, 50, 50);
    EXPECT_GT(center.r, 200);
    EXPECT_GT(center.g, 200);
    const Pixel right = PixelAt(pixels, 100, 64, 50);
    const Pixel left = PixelAt(pixels, 100, 36, 50);
    const Pixel up = PixelAt(pixels, 100, 50, 36);
    const Pixel down = PixelAt(pixels, 100, 50, 64);
    EXPECT_GT(right.r + right.g, 8);
    EXPECT_GT(left.r + left.g, 8);
    EXPECT_GT(up.r + up.g, 8);
    EXPECT_GT(down.r + down.g, 8);
}

TEST(TgfxRenderAdapterTest, LayerOpacityFadesDropShadow) {
    auto adapter = TgfxRenderAdapter::Make(100, 100);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    auto render = [&](float opacity) {
        SceneState state = MakeCenteredWhiteRect();
        state.backgroundColor = Color{0, 0, 0, 0};
        state.layers[0].opacity = opacity;
        state.layers[0].layerStyles.push_back(std::make_shared<DropShadowStyle>());
        adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
        PlayCommands(BuildCommands(state), *adapter);
        adapter->endFrame();
        std::vector<uint8_t> pixels;
        EXPECT_TRUE(adapter->ReadPixels(pixels));
        return PixelAt(pixels, 100, 64, 64);
    };

    const Pixel full = render(1.0f);
    const Pixel faded = render(0.5f);
    EXPECT_NEAR(static_cast<int>(faded.a), static_cast<int>(full.a) / 2, 20);
}

TEST(TgfxRenderAdapterTest, DropShadowSpreadChangesPixels) {
    auto adapter = TgfxRenderAdapter::Make(100, 100);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    auto render = [&](float spread) {
        SceneState state = MakeCenteredWhiteRect();
        auto shadow = std::make_shared<DropShadowStyle>();
        shadow->distance.setStaticValue(0.0f);
        shadow->size.setStaticValue(8.0f);
        shadow->spread.setStaticValue(spread);
        shadow->color.setStaticValue(Color{1, 0, 0, 1});
        state.layers[0].layerStyles.push_back(std::move(shadow));
        adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
        PlayCommands(BuildCommands(state), *adapter);
        adapter->endFrame();
        std::vector<uint8_t> pixels;
        EXPECT_TRUE(adapter->ReadPixels(pixels));
        return pixels;
    };

    const std::vector<uint8_t> none = render(0.0f);
    const std::vector<uint8_t> full = render(1.0f);
    EXPECT_NE(none, full);
}

TEST(TgfxRenderAdapterTest, StrokeOutsideDrawsAroundRect) {
    auto adapter = TgfxRenderAdapter::Make(100, 100);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    SceneState state = MakeCenteredWhiteRect();
    auto stroke = std::make_shared<LayerStrokeStyle>();
    stroke->size.setStaticValue(6.0f);
    state.layers[0].layerStyles.push_back(std::move(stroke));

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();
    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const Pixel center = PixelAt(pixels, 100, 50, 50);
    EXPECT_GT(center.r, 200);
    EXPECT_GT(center.g, 200);
    EXPECT_GT(center.b, 200);
    const Pixel ring = PixelAt(pixels, 100, 63, 50);
    EXPECT_GT(static_cast<int>(ring.r), static_cast<int>(ring.g) + 20);
    EXPECT_GT(static_cast<int>(ring.r), static_cast<int>(ring.b) + 20);
}

TEST(TgfxRenderAdapterTest, BlurThenDropShadowHasShadowInBleed) {
    auto adapter = TgfxRenderAdapter::Make(100, 100);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    SceneState state = MakeCenteredWhiteRect();
    auto blur = std::make_shared<GaussianBlurEffect>();
    blur->blurriness.setStaticValue(16.0f);
    state.layers[0].effects.push_back(std::move(blur));

    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();
    std::vector<uint8_t> blurPixels;
    ASSERT_TRUE(adapter->ReadPixels(blurPixels));
    const Pixel blurOnly = PixelAt(blurPixels, 100, 68, 50);

    auto shadow = std::make_shared<DropShadowStyle>();
    shadow->color.setStaticValue(Color{1, 0, 0, 1});
    state.layers[0].layerStyles.push_back(std::move(shadow));
    adapter->beginFrame(100, 100, state.backgroundColor, state.cornerRadius);
    PlayCommands(BuildCommands(state), *adapter);
    adapter->endFrame();
    std::vector<uint8_t> bothPixels;
    ASSERT_TRUE(adapter->ReadPixels(bothPixels));
    const Pixel withShadow = PixelAt(bothPixels, 100, 68, 50);
    EXPECT_NE(blurOnly.r, withShadow.r);
}

TEST(TgfxRenderAdapterTest, NestedBeginLayerDoesNotFreeUnallocatedCanvas) {
    auto adapter = TgfxRenderAdapter::Make(64, 64);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }
    adapter->beginFrame(64, 64, Color{0, 0, 0, 1}, 0.0f);
    DrawCommandList commands;
    DrawCommand beginLayer;
    beginLayer.type = DrawCommandType::BeginLayer;
    commands.push_back(beginLayer);
    commands.push_back(beginLayer);
    DrawCommand endLayer;
    endLayer.type = DrawCommandType::EndLayer;
    commands.push_back(endLayer);
    commands.push_back(endLayer);
    PlayCommands(commands, *adapter);
    adapter->endFrame();
}
