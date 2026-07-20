#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/animation/Keyframe.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/PrecompContent.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeFill.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/render/SceneEvaluator.h"

using motion::Composition;
using motion::Document;
using motion::EntityId;
using motion::Expected;
using motion::Keyframe;
using motion::Layer;
using motion::LayerType;
using motion::PrecompContent;
using motion::SceneEvaluator;
using motion::SceneState;
using motion::ShapeContent;
using motion::ShapeFill;
using motion::ShapeRect;
using motion::Vec2;

namespace {

// Adds a shape layer with a filled 10x10 rect at the given center.
Layer* AddRectLayer(Document& document, EntityId compositionId, Vec2 center) {
    Layer* layer =
        document.addLayer(compositionId, std::make_unique<Layer>(LayerType::Shape));
    layer->outPoint = 1000;
    auto* content = static_cast<ShapeContent*>(layer->content.get());
    auto rect = std::make_unique<ShapeRect>();
    rect->position.setStaticValue(center);
    rect->size.setStaticValue(Vec2{10, 10});
    content->elements.push_back(std::move(rect));
    content->elements.push_back(std::make_unique<ShapeFill>());
    return layer;
}

Layer* AddPrecompLayer(Document& document, EntityId hostId, EntityId sourceId) {
    Layer* layer =
        document.addLayer(hostId, std::make_unique<Layer>(LayerType::Precomp));
    layer->outPoint = 1000;
    static_cast<PrecompContent*>(layer->content.get())->compositionId = sourceId;
    return layer;
}

}  // namespace

TEST(PrecompTest, FlattensSublayerKeepingItsId) {
    Document document;
    Composition* inner = document.addComposition(std::make_unique<Composition>());
    Layer* rectLayer = AddRectLayer(document, inner->id, Vec2{0, 0});
    Composition* main = document.addComposition(std::make_unique<Composition>());
    Layer* precomp = AddPrecompLayer(document, main->id, inner->id);
    precomp->transform.position.setStaticValue(Vec2{100, 0});

    Expected<SceneState> result = SceneEvaluator::Evaluate(document, main->id, 0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers.size(), 1u);
    EXPECT_EQ(result->layers[0].id, rectLayer->id);  // sublayer id preserved
    EXPECT_EQ(result->layers[0].shapeItems[0].path.vertices[0].point, (Vec2{95, -5}));
}

TEST(PrecompTest, TimeMappingAppliesStretchAndStart) {
    // innerTime = (outer - inPoint) * timeStretch + startTime
    Document document;
    Composition* inner = document.addComposition(std::make_unique<Composition>());
    Layer* rectLayer = AddRectLayer(document, inner->id, Vec2{0, 0});
    Keyframe<Vec2> from;
    from.time = 0;
    from.value = Vec2{0, 0};
    Keyframe<Vec2> to;
    to.time = 20;
    to.value = Vec2{100, 0};
    rectLayer->transform.position.addKeyframe(from);
    rectLayer->transform.position.addKeyframe(to);

    Composition* main = document.addComposition(std::make_unique<Composition>());
    Layer* precomp = AddPrecompLayer(document, main->id, inner->id);
    precomp->inPoint = 10;
    precomp->startTime = 5;
    precomp->timeStretch = 2;

    // outer 15 -> inner (15-10)*2+5 = 15 -> position x = 75 -> rect left = 70.
    Expected<SceneState> result = SceneEvaluator::Evaluate(document, main->id, 15);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers.size(), 1u);
    EXPECT_EQ(result->layers[0].shapeItems[0].path.vertices[0].point, (Vec2{70, -5}));
}

TEST(PrecompTest, ThreeLevelNestingComposesTransformsAndOpacity) {
    Document document;
    Composition* deep = document.addComposition(std::make_unique<Composition>());
    AddRectLayer(document, deep->id, Vec2{0, 0});

    Composition* mid = document.addComposition(std::make_unique<Composition>());
    Layer* midPrecomp = AddPrecompLayer(document, mid->id, deep->id);
    midPrecomp->transform.position.setStaticValue(Vec2{10, 0});
    midPrecomp->transform.opacity.setStaticValue(0.5f);

    Composition* main = document.addComposition(std::make_unique<Composition>());
    Layer* mainPrecomp = AddPrecompLayer(document, main->id, mid->id);
    mainPrecomp->transform.position.setStaticValue(Vec2{20, 0});
    mainPrecomp->transform.opacity.setStaticValue(0.5f);

    Expected<SceneState> result = SceneEvaluator::Evaluate(document, main->id, 0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers.size(), 1u);
    // rect left = 0 + 10 + 20 - 5
    EXPECT_EQ(result->layers[0].shapeItems[0].path.vertices[0].point, (Vec2{25, -5}));
    EXPECT_FLOAT_EQ(result->layers[0].opacity, 0.25f);
}

TEST(PrecompTest, MissingTargetProducesNoLayers) {
    Document document;
    Composition* main = document.addComposition(std::make_unique<Composition>());
    AddPrecompLayer(document, main->id, EntityId{999});

    Expected<SceneState> result = SceneEvaluator::Evaluate(document, main->id, 0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result->layers.empty());
}

TEST(PrecompTest, SelfReferencingCycleTerminates) {
    Document document;
    Composition* loop = document.addComposition(std::make_unique<Composition>());
    AddPrecompLayer(document, loop->id, loop->id);

    Expected<SceneState> result = SceneEvaluator::Evaluate(document, loop->id, 0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result->layers.empty());
}

TEST(PrecompTest, TwoCompositionCycleTerminates) {
    Document document;
    Composition* a = document.addComposition(std::make_unique<Composition>());
    Composition* b = document.addComposition(std::make_unique<Composition>());
    AddPrecompLayer(document, a->id, b->id);
    AddPrecompLayer(document, b->id, a->id);

    Expected<SceneState> result = SceneEvaluator::Evaluate(document, a->id, 0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result->layers.empty());
}
