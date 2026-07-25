#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapeGroup.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/render/SceneEvaluator.h"

using motion::Color;
using motion::Composition;
using motion::Document;
using motion::EntityId;
using motion::Expected;
using motion::FillStyle;
using motion::Layer;
using motion::LayerType;
using motion::SceneEvaluator;
using motion::SceneState;
using motion::ShapeContent;
using motion::ShapeEllipse;
using motion::ShapeGroup;
using motion::ShapeRect;
using motion::StrokeStyle;
using motion::Vec2;

namespace {

struct RectScene {
    Document document;
    Composition *composition;
    Layer *layer;
    ShapeRect *rect = nullptr;
    FillStyle *fill = nullptr;

    RectScene() {
        composition = document.addComposition(std::make_unique<Composition>());
        composition->duration = 100;
        layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
        layer->outPoint = 100;
        auto *content = static_cast<ShapeContent *>(layer->content.get());
        auto rectElement = std::make_unique<ShapeRect>();
        rect = rectElement.get();
        rect->position.setStaticValue(Vec2{100, 50});
        rect->size.setStaticValue(Vec2{40, 20});
        content->geometry = std::move(rectElement);
        auto fillElement = std::make_unique<FillStyle>();
        fill = fillElement.get();
        fill->color.setStaticValue(Color{1, 0, 0, 1});
        layer->styles.push_back(std::move(fillElement));
    }

    Expected<SceneState, std::string> Evaluate(motion::FrameTime time) {
        return SceneEvaluator::Evaluate(document, composition->id, time);
    }

    Expected<SceneState, std::string> EvaluatePreview(motion::PreviewTime time) {
        return SceneEvaluator::EvaluatePreview(document, composition->id, time);
    }
};

}  // namespace

TEST(SceneEvaluatorTest, MissingCompositionFails) {
    Document document;
    Expected<SceneState, std::string> result = SceneEvaluator::Evaluate(document, EntityId{1}, 0);
    ASSERT_FALSE(result.hasValue());
    EXPECT_NE(result.error().find("composition not found"), std::string::npos);
}

TEST(SceneEvaluatorTest, EmptyCompositionProducesViewportAndBackground) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    composition->width = 640;
    composition->height = 480;
    composition->backgroundColor = Color{0.5f, 0.5f, 0.5f, 1};
    composition->cornerRadius = 32.0f;

    Expected<SceneState, std::string> result = SceneEvaluator::Evaluate(document, composition->id, 0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result->viewportWidth, 640);
    EXPECT_EQ(result->viewportHeight, 480);
    EXPECT_EQ(result->backgroundColor, (Color{0.5f, 0.5f, 0.5f, 1}));
    EXPECT_FLOAT_EQ(result->cornerRadius, 32.0f);
    EXPECT_TRUE(result->layers.empty());
}

TEST(SceneEvaluatorTest, RectFillProducesOneWorldSpaceItem) {
    RectScene scene;
    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers.size(), 1u);
    const auto &evaluated = result->layers[0];
    EXPECT_EQ(evaluated.id, scene.layer->id);
    EXPECT_FLOAT_EQ(evaluated.opacity, 1.0f);
    ASSERT_EQ(evaluated.shapeItems.size(), 1u);
    const auto &item = evaluated.shapeItems[0];
    EXPECT_FALSE(item.isStroke);
    EXPECT_EQ(item.paint.color, (Color{1, 0, 0, 1}));
    ASSERT_EQ(item.path.vertices.size(), 4u);
    EXPECT_TRUE(item.path.closed);
    EXPECT_EQ(item.path.vertices[0].point, (Vec2{80, 40}));
    EXPECT_EQ(item.path.vertices[1].point, (Vec2{120, 40}));
    EXPECT_EQ(item.path.vertices[2].point, (Vec2{120, 60}));
    EXPECT_EQ(item.path.vertices[3].point, (Vec2{80, 60}));
}

TEST(SceneEvaluatorTest, MultipleFillsProduceItemsWithOwnBlendModes) {
    RectScene scene;
    auto secondFill = std::make_unique<FillStyle>();
    secondFill->color.setStaticValue(Color{0, 0, 1, 1});
    secondFill->blendMode = motion::BlendMode::Screen;
    scene.layer->styles.push_back(std::move(secondFill));

    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers.size(), 1u);
    ASSERT_EQ(result->layers[0].shapeItems.size(), 2u);
    const auto &first = result->layers[0].shapeItems[0];
    const auto &second = result->layers[0].shapeItems[1];
    EXPECT_EQ(first.paint.color, (Color{1, 0, 0, 1}));
    EXPECT_EQ(first.paint.blendMode, motion::BlendMode::Normal);
    EXPECT_EQ(second.paint.color, (Color{0, 0, 1, 1}));
    EXPECT_EQ(second.paint.blendMode, motion::BlendMode::Screen);
}

TEST(SceneEvaluatorTest, StrokeItemKeepsLayerBlendMode) {
    RectScene scene;
    scene.layer->blendMode = motion::BlendMode::Multiply;
    auto stroke = std::make_unique<StrokeStyle>();
    stroke->width.setStaticValue(4.0f);
    scene.layer->styles.push_back(std::move(stroke));

    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers[0].shapeItems.size(), 2u);
    const auto &fillItem = result->layers[0].shapeItems[0];
    const auto &strokeItem = result->layers[0].shapeItems[1];
    EXPECT_FALSE(fillItem.isStroke);
    EXPECT_EQ(fillItem.paint.blendMode, motion::BlendMode::Normal);
    EXPECT_TRUE(strokeItem.isStroke);
    EXPECT_EQ(strokeItem.paint.blendMode, motion::BlendMode::Multiply);
}

TEST(SceneEvaluatorTest, LayerTransformAppliesToPath) {
    RectScene scene;
    scene.layer->transform.position.setStaticValue(Vec2{10, 20});
    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    const auto &item = result->layers[0].shapeItems[0];
    EXPECT_EQ(item.path.vertices[0].point, (Vec2{90, 60}));
}

TEST(SceneEvaluatorTest, ParentTransformChain) {
    RectScene scene;
    Layer *parent =
        scene.document.addLayer(scene.composition->id, std::make_unique<Layer>(LayerType::Group));
    parent->outPoint = 100;
    parent->transform.position.setStaticValue(Vec2{100, 0});
    scene.layer->parentId = parent->id;

    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers.size(), 1u);  // Group parent produces no items
    const auto &item = result->layers[0].shapeItems[0];
    EXPECT_EQ(item.path.vertices[0].point, (Vec2{180, 40}));
}

TEST(SceneEvaluatorTest, OpacityInheritsFromParent) {
    RectScene scene;
    Layer *parent =
        scene.document.addLayer(scene.composition->id, std::make_unique<Layer>(LayerType::Group));
    parent->outPoint = 100;
    parent->transform.opacity.setStaticValue(0.5f);
    scene.layer->parentId = parent->id;
    scene.layer->transform.opacity.setStaticValue(0.5f);

    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_FLOAT_EQ(result->layers[0].opacity, 0.25f);
}

TEST(SceneEvaluatorTest, OutOfTimeRangeLayerSkipped) {
    RectScene scene;
    scene.layer->inPoint = 10;
    scene.layer->outPoint = 20;

    Expected<SceneState, std::string> before = scene.Evaluate(5);
    ASSERT_TRUE(before.hasValue());
    EXPECT_TRUE(before->layers.empty());

    Expected<SceneState, std::string> inside = scene.Evaluate(15);
    ASSERT_TRUE(inside.hasValue());
    EXPECT_EQ(inside->layers.size(), 1u);

    Expected<SceneState, std::string> atOutPoint = scene.Evaluate(20);  // exclusive
    ASSERT_TRUE(atOutPoint.hasValue());
    EXPECT_TRUE(atOutPoint->layers.empty());
}

TEST(SceneEvaluatorTest, InvisibleLayerSkipped) {
    RectScene scene;
    scene.layer->visible = false;
    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result->layers.empty());
}

TEST(SceneEvaluatorTest, GroupTransformComposesIntoPath) {
    RectScene scene;
    auto *content = static_cast<ShapeContent *>(scene.layer->content.get());
    auto group = std::make_unique<ShapeGroup>();
    group->transform.position.setStaticValue(Vec2{5, 5});
    auto rect = std::make_unique<ShapeRect>();
    rect->position.setStaticValue(Vec2{0, 0});
    rect->size.setStaticValue(Vec2{10, 10});
    group->elements.push_back(std::move(rect));
    content->geometry = std::move(group);

    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    const auto &item = result->layers[0].shapeItems[0];
    EXPECT_EQ(item.path.vertices[0].point, (Vec2{0, 0}));
    EXPECT_EQ(item.path.vertices[2].point, (Vec2{10, 10}));
}

TEST(SceneEvaluatorTest, StrokeItemCarriesWidthAndCaps) {
    RectScene scene;
    auto stroke = std::make_unique<StrokeStyle>();
    stroke->width.setStaticValue(3.0f);
    stroke->cap = motion::LineCap::Round;
    scene.layer->styles.push_back(std::move(stroke));

    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers[0].shapeItems.size(), 2u);
    const auto &strokeItem = result->layers[0].shapeItems[1];
    EXPECT_TRUE(strokeItem.isStroke);
    EXPECT_FLOAT_EQ(strokeItem.strokeWidth, 3.0f);
    EXPECT_EQ(strokeItem.cap, motion::LineCap::Round);
}

TEST(SceneEvaluatorTest, EllipseProducesFourVertexClosedPath) {
    RectScene scene;
    auto *content = static_cast<ShapeContent *>(scene.layer->content.get());
    auto ellipse = std::make_unique<ShapeEllipse>();
    ellipse->position.setStaticValue(Vec2{0, 0});
    ellipse->size.setStaticValue(Vec2{20, 10});
    content->geometry = std::move(ellipse);

    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    const auto &path = result->layers[0].shapeItems[0].path;
    ASSERT_EQ(path.vertices.size(), 4u);
    EXPECT_TRUE(path.closed);
    EXPECT_TRUE(motion::ApproxEqual(path.vertices[0].point, Vec2{10, 0}));
    EXPECT_TRUE(motion::ApproxEqual(path.vertices[1].point, Vec2{0, 5}));
    EXPECT_TRUE(motion::ApproxEqual(path.vertices[2].point, Vec2{-10, 0}));
    EXPECT_TRUE(motion::ApproxEqual(path.vertices[3].point, Vec2{0, -5}));
}

TEST(SceneEvaluatorTest, AnimatedTransformEvaluatedAtTime) {
    RectScene scene;
    motion::Keyframe<Vec2> from;
    from.time = 0;
    from.value = Vec2{0, 0};
    motion::Keyframe<Vec2> to;
    to.time = 10;
    to.value = Vec2{100, 0};
    scene.layer->transform.position.addKeyframe(from);
    scene.layer->transform.position.addKeyframe(to);

    Expected<SceneState, std::string> mid = scene.Evaluate(5);
    ASSERT_TRUE(mid.hasValue());
    const auto &item = mid->layers[0].shapeItems[0];
    EXPECT_EQ(item.path.vertices[0].point, (Vec2{130, 40}));
}

TEST(SceneEvaluatorTest, PreviewEvaluatesFractionalTransformTime) {
    RectScene scene;
    motion::Keyframe<Vec2> from;
    from.time = 0;
    from.value = Vec2{0, 0};
    motion::Keyframe<Vec2> to;
    to.time = 10;
    to.value = Vec2{100, 0};
    scene.layer->transform.position.addKeyframe(from);
    scene.layer->transform.position.addKeyframe(to);

    Expected<SceneState, std::string> quarter = scene.EvaluatePreview(2.5);
    ASSERT_TRUE(quarter.hasValue());
    const auto &item = quarter->layers[0].shapeItems[0];
    EXPECT_EQ(item.path.vertices[0].point, (Vec2{105, 40}));
}
