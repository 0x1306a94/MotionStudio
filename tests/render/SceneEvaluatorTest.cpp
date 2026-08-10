#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/VectorNetworkConvert.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/MaskMode.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/TextContent.h"
#include "MotionStudio/model/TrackMatteType.h"
#include "MotionStudio/render/SceneEvaluator.h"
#include "MotionStudio/render/ShapeGeometry.h"

using motion::BezierPath;
using motion::BezierPathToVectorNetwork;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::EntityId;
using motion::EvaluatedShapeItem;
using motion::Expected;
using motion::FillStyle;
using motion::Layer;
using motion::LayerType;
using motion::MakeSingleContour;
using motion::Mat3;
using motion::SceneEvaluator;
using motion::SceneState;
using motion::ShapeContent;
using motion::ShapeEllipse;
using motion::ShapePath;
using motion::ShapeRect;
using motion::StrokeStyle;
using motion::Vec2;
using motion::VectorNetwork;

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

BezierPath MakeShiftedSegment(float x0, float x1) {
    BezierPath path = MakeSingleContour({{{x0, 0}, {0, 0}, {0, 0}}, {{x1, 0}, {0, 0}, {0, 0}}}, false);
    return path;
}

struct PathScene {
    Document document;
    Composition *composition = nullptr;
    Layer *layer = nullptr;
    ShapePath *pathShape = nullptr;

    PathScene() {
        composition = document.addComposition(std::make_unique<Composition>());
        composition->duration = 100;
        layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
        layer->outPoint = 100;
        auto *content = static_cast<ShapeContent *>(layer->content.get());
        auto element = std::make_unique<ShapePath>();
        pathShape = element.get();
        content->geometry = std::move(element);
        auto fill = std::make_unique<FillStyle>();
        fill->color.setStaticValue(Color{1, 0, 0, 1});
        layer->styles.push_back(std::move(fill));
    }

    Expected<SceneState, std::string> Evaluate(motion::FrameTime time) {
        return SceneEvaluator::Evaluate(document, composition->id, time);
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

TEST(SceneEvaluatorTest, RectFillProducesOneLocalSpaceItem) {
    RectScene scene;
    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers.size(), 1u);
    const auto &evaluated = result->layers[0];
    EXPECT_EQ(evaluated.id, scene.layer->id);
    EXPECT_FLOAT_EQ(evaluated.opacity, 1.0f);
    EXPECT_EQ(evaluated.worldTransform, Mat3::Identity());
    ASSERT_EQ(evaluated.shapeItems.size(), 1u);
    const auto &item = evaluated.shapeItems[0];
    EXPECT_FALSE(item.isStroke);
    EXPECT_EQ(item.paint.color, (Color{1, 0, 0, 1}));
    EXPECT_EQ(item.geometry.kind, motion::ShapeGeometryKind::Rect);
    EXPECT_EQ(item.geometry.center, (Vec2{100, 50}));
    EXPECT_EQ(item.geometry.size, (Vec2{40, 20}));
    EXPECT_FLOAT_EQ(item.geometry.cornerRadius, 0.0f);
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

TEST(SceneEvaluatorTest, StrokeItemUsesOwnBlendMode) {
    RectScene scene;
    scene.layer->blendMode = motion::BlendMode::Multiply;
    auto stroke = std::make_unique<StrokeStyle>();
    stroke->width.setStaticValue(4.0f);
    stroke->blendMode = motion::BlendMode::Screen;
    scene.layer->styles.push_back(std::move(stroke));

    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers[0].shapeItems.size(), 2u);
    const auto &fillItem = result->layers[0].shapeItems[0];
    const auto &strokeItem = result->layers[0].shapeItems[1];
    EXPECT_FALSE(fillItem.isStroke);
    EXPECT_EQ(fillItem.paint.blendMode, motion::BlendMode::Normal);
    EXPECT_TRUE(strokeItem.isStroke);
    EXPECT_EQ(strokeItem.paint.blendMode, motion::BlendMode::Screen);
}

TEST(SceneEvaluatorTest, StrokeItemCarriesPositionAndTrim) {
    RectScene scene;
    auto stroke = std::make_unique<StrokeStyle>();
    stroke->position = motion::StrokePosition::Inside;
    stroke->trimStart.setStaticValue(0.25f);
    stroke->trimEnd.setStaticValue(0.75f);
    stroke->trimOffset.setStaticValue(90.0f);
    scene.layer->styles.push_back(std::move(stroke));

    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers[0].shapeItems.size(), 2u);
    const auto &strokeItem = result->layers[0].shapeItems[1];
    EXPECT_EQ(strokeItem.stroke.position, motion::StrokePosition::Inside);
    EXPECT_FLOAT_EQ(strokeItem.stroke.trimStart, 0.25f);
    EXPECT_FLOAT_EQ(strokeItem.stroke.trimEnd, 0.75f);
    EXPECT_FLOAT_EQ(strokeItem.stroke.trimOffset, 90.0f);
}

TEST(SceneEvaluatorTest, StrokeBeforeFillStillPaintsFillThenStroke) {
    RectScene scene;
    // RectScene already has one Fill at styles[0]. Prepend a Stroke so disk
    // order is [Stroke, Fill] — the pen-then-add-fill case.
    auto stroke = std::make_unique<StrokeStyle>();
    stroke->position = motion::StrokePosition::Inside;
    stroke->width.setStaticValue(9.0f);
    stroke->color.setStaticValue(Color{1, 1, 1, 1});
    scene.layer->styles.insert(scene.layer->styles.begin(), std::move(stroke));
    ASSERT_EQ(scene.layer->styles[0]->type(), motion::LayerStyleType::Stroke);
    ASSERT_EQ(scene.layer->styles[1]->type(), motion::LayerStyleType::Fill);

    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers[0].shapeItems.size(), 2u);
    EXPECT_FALSE(result->layers[0].shapeItems[0].isStroke);
    EXPECT_TRUE(result->layers[0].shapeItems[1].isStroke);
    EXPECT_EQ(result->layers[0].shapeItems[1].stroke.position, motion::StrokePosition::Inside);
}

TEST(SceneEvaluatorTest, InterleavedFillsKeepRelativeOrderBeforeStrokes) {
    RectScene scene;
    // Disk: [Fill0, Stroke, Fill1] → paint Fill0, Fill1, Stroke
    auto stroke = std::make_unique<StrokeStyle>();
    stroke->width.setStaticValue(2.0f);
    scene.layer->styles.push_back(std::move(stroke));
    auto fill1 = std::make_unique<FillStyle>();
    fill1->color.setStaticValue(Color{0, 1, 0, 1});
    scene.layer->styles.push_back(std::move(fill1));

    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers[0].shapeItems.size(), 3u);
    EXPECT_FALSE(result->layers[0].shapeItems[0].isStroke);
    EXPECT_FLOAT_EQ(result->layers[0].shapeItems[0].paint.color.r, 1.0f);  // Fill0 red
    EXPECT_FALSE(result->layers[0].shapeItems[1].isStroke);
    EXPECT_FLOAT_EQ(result->layers[0].shapeItems[1].paint.color.g, 1.0f);  // Fill1 green
    EXPECT_TRUE(result->layers[0].shapeItems[2].isStroke);
}

TEST(SceneEvaluatorTest, TextStrokeBeforeFillStillPaintsFillThenStroke) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    composition->duration = 100;
    Layer *layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Text));
    layer->outPoint = 100;
    auto *text = static_cast<motion::TextContent *>(layer->content.get());
    text->text.setStaticValue("Hi");

    auto stroke = std::make_unique<StrokeStyle>();
    stroke->width.setStaticValue(2.0f);
    stroke->color.setStaticValue(Color{1, 1, 1, 1});
    layer->styles.push_back(std::move(stroke));
    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{0, 0, 0, 1});
    layer->styles.push_back(std::move(fill));

    Expected<SceneState, std::string> result = SceneEvaluator::Evaluate(document, composition->id, 0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(result->layers[0].textItem.has_value());
    const auto &styles = result->layers[0].textItem->styles;
    ASSERT_EQ(styles.size(), 2u);
    EXPECT_FALSE(styles[0].isStroke);
    EXPECT_TRUE(styles[1].isStroke);
}

TEST(SceneEvaluatorTest, LayerTransformStoredSeparatelyFromPath) {
    RectScene scene;
    scene.layer->transform.position.setStaticValue(Vec2{10, 20});
    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    const auto &evaluated = result->layers[0];
    EXPECT_EQ(evaluated.worldTransform, Mat3::Translate(Vec2{10, 20}));
    EXPECT_EQ(evaluated.shapeItems[0].geometry.kind, motion::ShapeGeometryKind::Rect);
    EXPECT_EQ(evaluated.shapeItems[0].geometry.center, (Vec2{100, 50}));
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
    const auto &evaluated = result->layers[0];
    EXPECT_EQ(evaluated.worldTransform, Mat3::Translate(Vec2{100, 0}));
    EXPECT_EQ(evaluated.shapeItems[0].geometry.center, (Vec2{100, 50}));
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

// Default layer outPoint equals composition duration (exclusive). Scrubbing must
// stop at duration-1 so the last inclusive frame still draws layers.
TEST(SceneEvaluatorTest, LastInclusiveFrameDrawsWhenOutEqualsDuration) {
    RectScene scene;  // duration=100, outPoint=100
    Expected<SceneState, std::string> lastInclusive = scene.Evaluate(99);
    ASSERT_TRUE(lastInclusive.hasValue());
    EXPECT_EQ(lastInclusive->layers.size(), 1u);

    Expected<SceneState, std::string> atDuration = scene.Evaluate(100);
    ASSERT_TRUE(atDuration.hasValue());
    EXPECT_TRUE(atDuration->layers.empty());
    EXPECT_EQ(atDuration->backgroundColor.r, scene.composition->backgroundColor.r);
}

TEST(SceneEvaluatorTest, InvisibleLayerSkipped) {
    RectScene scene;
    scene.layer->visible = false;
    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result->layers.empty());
}

TEST(SceneEvaluatorTest, LayerGroupParentKeepsChildPathLocal) {
    RectScene scene;
    scene.rect->position.setStaticValue(Vec2{0, 0});
    scene.rect->size.setStaticValue(Vec2{10, 10});
    Layer *parent =
        scene.document.addLayer(scene.composition->id, std::make_unique<Layer>(LayerType::Group));
    parent->outPoint = 100;
    parent->transform.position.setStaticValue(Vec2{5, 5});
    scene.layer->parentId = parent->id;

    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    const auto &evaluated = result->layers[0];
    EXPECT_EQ(evaluated.worldTransform, Mat3::Translate(Vec2{5, 5}));
    EXPECT_EQ(evaluated.shapeItems[0].geometry.kind, motion::ShapeGeometryKind::Rect);
    EXPECT_EQ(evaluated.shapeItems[0].geometry.center, (Vec2{0, 0}));
    EXPECT_EQ(evaluated.shapeItems[0].geometry.size, (Vec2{10, 10}));
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
    EXPECT_FLOAT_EQ(strokeItem.stroke.width, 3.0f);
    EXPECT_EQ(strokeItem.stroke.cap, motion::LineCap::Round);
}

TEST(SceneEvaluatorTest, EllipseKeepsParametricGeometry) {
    RectScene scene;
    auto *content = static_cast<ShapeContent *>(scene.layer->content.get());
    auto ellipse = std::make_unique<ShapeEllipse>();
    ellipse->position.setStaticValue(Vec2{0, 0});
    ellipse->size.setStaticValue(Vec2{20, 10});
    content->geometry = std::move(ellipse);

    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    const auto &geometry = result->layers[0].shapeItems[0].geometry;
    EXPECT_EQ(geometry.kind, motion::ShapeGeometryKind::Ellipse);
    EXPECT_EQ(geometry.center, (Vec2{0, 0}));
    EXPECT_EQ(geometry.size, (Vec2{20, 10}));
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
    const auto &evaluated = mid->layers[0];
    EXPECT_EQ(evaluated.worldTransform, Mat3::Translate(Vec2{50, 0}));
    EXPECT_EQ(evaluated.shapeItems[0].geometry.center, (Vec2{100, 50}));
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
    const auto &evaluated = quarter->layers[0];
    EXPECT_EQ(evaluated.worldTransform, Mat3::Translate(Vec2{25, 0}));
    EXPECT_EQ(evaluated.shapeItems[0].geometry.center, (Vec2{100, 50}));
}

TEST(SceneEvaluatorTest, EvaluatesMaskScalars) {
    RectScene scene;
    motion::Mask mask;
    motion::BezierPath path = MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{10, 10}, {}, {}}}, true);
    mask.path.setStaticValue(BezierPathToVectorNetwork(path));
    mask.mode = motion::MaskMode::Intersect;
    mask.opacity.setStaticValue(0.5f);
    mask.inverted = true;
    mask.feather.setStaticValue(3.0f);
    mask.expansion.setStaticValue(-2.0f);
    scene.layer->masks.push_back(mask);

    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers.size(), 1u);
    ASSERT_EQ(result->layers[0].masks.size(), 1u);
    const auto &evaluatedMask = result->layers[0].masks[0];
    EXPECT_EQ(evaluatedMask.mode, motion::MaskMode::Intersect);
    EXPECT_FLOAT_EQ(evaluatedMask.opacity, 0.5f);
    EXPECT_TRUE(evaluatedMask.inverted);
    EXPECT_FLOAT_EQ(evaluatedMask.feather, 3.0f);
    EXPECT_FLOAT_EQ(evaluatedMask.expansion, -2.0f);
    ASSERT_FALSE(evaluatedMask.path.contours.empty());
    EXPECT_TRUE(evaluatedMask.path.contours[0].closed);
    // Fill faces are densely sampled polylines (not authoring vertex arity).
    EXPECT_GE(evaluatedMask.path.contours[0].vertices.size(), 3u);
}

TEST(SceneEvaluatorTest, ResolvesTrackMatteAndMarksSource) {
    RectScene scene;
    Layer *matteLayer =
        scene.document.addLayer(scene.composition->id, std::make_unique<Layer>(LayerType::Shape));
    matteLayer->outPoint = 100;
    auto *matteContent = static_cast<ShapeContent *>(matteLayer->content.get());
    auto rectElement = std::make_unique<ShapeRect>();
    rectElement->size.setStaticValue(Vec2{20, 20});
    matteContent->geometry = std::move(rectElement);
    auto fillElement = std::make_unique<FillStyle>();
    fillElement->color.setStaticValue(Color{1, 1, 1, 1});
    matteLayer->styles.push_back(std::move(fillElement));

    scene.layer->trackMatteType = motion::TrackMatteType::Alpha;
    scene.layer->trackMatteLayerId = matteLayer->id;

    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers.size(), 2u);

    const motion::EvaluatedLayer *target = nullptr;
    const motion::EvaluatedLayer *source = nullptr;
    for (const auto &layer : result->layers) {
        if (layer.id == scene.layer->id) {
            target = &layer;
        }
        if (layer.id == matteLayer->id) {
            source = &layer;
        }
    }
    ASSERT_NE(target, nullptr);
    ASSERT_NE(source, nullptr);
    EXPECT_EQ(target->trackMatteType, motion::TrackMatteType::Alpha);
    EXPECT_EQ(target->matteSourceId, matteLayer->id);
    EXPECT_FALSE(target->usedAsMatteOnly);
    EXPECT_TRUE(source->usedAsMatteOnly);
}

TEST(SceneEvaluatorTest, SelfTrackMatteIsIgnored) {
    RectScene scene;
    scene.layer->trackMatteType = motion::TrackMatteType::Luma;
    scene.layer->trackMatteLayerId = scene.layer->id;

    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers.size(), 1u);
    EXPECT_EQ(result->layers[0].trackMatteType, motion::TrackMatteType::None);
    EXPECT_FALSE(result->layers[0].matteSourceId.isValid());
    EXPECT_FALSE(result->layers[0].usedAsMatteOnly);
}

TEST(SceneEvaluatorTest, AnimatedShapePathMorphsBetweenKeyframes) {
    PathScene scene;
    // Open segments have no fill faces; morph shows up on stroke edges.
    scene.layer->styles.clear();
    auto stroke = std::make_unique<StrokeStyle>();
    stroke->color.setStaticValue(Color{1, 0, 0, 1});
    stroke->width.setStaticValue(2.0f);
    scene.layer->styles.push_back(std::move(stroke));

    motion::Keyframe<VectorNetwork> from;
    from.time = 0;
    from.value = BezierPathToVectorNetwork(MakeShiftedSegment(0, 10));
    motion::Keyframe<VectorNetwork> to;
    to.time = 20;
    to.value = BezierPathToVectorNetwork(MakeShiftedSegment(20, 30));
    scene.pathShape->path.addKeyframe(from);
    scene.pathShape->path.addKeyframe(to);

    Expected<SceneState, std::string> mid = scene.Evaluate(10);
    ASSERT_TRUE(mid.hasValue());
    ASSERT_EQ(mid->layers.size(), 1u);
    ASSERT_FALSE(mid->layers[0].shapeItems.empty());
    EXPECT_TRUE(mid->layers[0].shapeItems[0].isStroke);

    const BezierPath stroked =
        motion::ShapeGeometryStrokePath(mid->layers[0].shapeItems[0].geometry);
    ASSERT_EQ(stroked.contours.size(), 1u);
    ASSERT_EQ(stroked.contours[0].vertices.size(), 2u);
    EXPECT_FLOAT_EQ(stroked.contours[0].vertices[0].point.x, 10.0f);
    EXPECT_FLOAT_EQ(stroked.contours[0].vertices[1].point.x, 20.0f);
}

TEST(SceneEvaluatorTest, TriangleFanFillAndStrokeContours) {
    PathScene scene;
    scene.layer->styles.clear();
    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(Color{1, 0, 0, 1});
    scene.layer->styles.push_back(std::move(fill));
    auto stroke = std::make_unique<StrokeStyle>();
    stroke->color.setStaticValue(Color{0, 0, 0, 1});
    stroke->width.setStaticValue(1.0f);
    scene.layer->styles.push_back(std::move(stroke));

    VectorNetwork network;
    network.vertices = {
        {1, {0, 2}},
        {2, {6, 0}},
        {3, {0, 8}},
        {4, {-6, 0}},
    };
    network.edges = {
        {1, 1, 2, {}, {}},
        {2, 1, 3, {}, {}},
        {3, 1, 4, {}, {}},
        {4, 2, 3, {}, {}},
        {5, 3, 4, {}, {}},
        {6, 4, 2, {}, {}},
    };
    scene.pathShape->path.setStaticValue(network);

    Expected<SceneState, std::string> result = scene.Evaluate(0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers.size(), 1u);
    ASSERT_EQ(result->layers[0].shapeItems.size(), 2u);

    const EvaluatedShapeItem &fillItem = result->layers[0].shapeItems[0];
    const EvaluatedShapeItem &strokeItem = result->layers[0].shapeItems[1];
    EXPECT_FALSE(fillItem.isStroke);
    EXPECT_TRUE(strokeItem.isStroke);
    EXPECT_EQ(fillItem.geometry.path.contours.size(), 3u);
    for (const BezierPath::Contour &contour : fillItem.geometry.path.contours) {
        EXPECT_TRUE(contour.closed);
        EXPECT_GE(contour.vertices.size(), 3u);
    }
    EXPECT_EQ(strokeItem.geometry.strokePath.contours.size(), 6u);
}
