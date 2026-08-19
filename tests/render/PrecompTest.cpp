#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/animation/Keyframe.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/PrecompContent.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/render/SceneEvaluator.h"

using motion::Composition;
using motion::Document;
using motion::EntityId;
using motion::EvaluatedLayer;
using motion::Expected;
using motion::FillStyle;
using motion::Keyframe;
using motion::Layer;
using motion::LayerType;
using motion::Mat3;
using motion::PrecompContent;
using motion::SceneEvaluator;
using motion::SceneState;
using motion::ShapeContent;
using motion::ShapeRect;
using motion::Vec2;

namespace {

// Adds a shape layer with a filled 10x10 rect at the given center.
Layer *AddRectLayer(Document &document, EntityId compositionId, Vec2 center) {
    Layer *layer =
        document.addLayer(compositionId, std::make_unique<Layer>(LayerType::Shape));
    layer->outPoint = 1000;
    auto *content = static_cast<ShapeContent *>(layer->content.get());
    auto rect = std::make_unique<ShapeRect>();
    rect->position.setStaticValue(center);
    rect->size.setStaticValue(Vec2{10, 10});
    content->geometry = std::move(rect);
    layer->styles.push_back(std::make_unique<FillStyle>());
    return layer;
}

Layer *AddPrecompLayer(Document &document, EntityId hostId, EntityId sourceId) {
    Layer *layer =
        document.addLayer(hostId, std::make_unique<Layer>(LayerType::Precomp));
    layer->outPoint = 1000;
    static_cast<PrecompContent *>(layer->content.get())->compositionId = sourceId;
    return layer;
}

const EvaluatedLayer *FindEvaluated(const SceneState &state, EntityId id) {
    for (const EvaluatedLayer &layer : state.layers) {
        if (layer.id == id) {
            return &layer;
        }
    }
    return nullptr;
}

}  // namespace

TEST(PrecompTest, FlattensSublayerKeepingItsId) {
    Document document;
    Composition *inner = document.addComposition(std::make_unique<Composition>());
    Layer *rectLayer = AddRectLayer(document, inner->id, Vec2{0, 0});
    Composition *main = document.addComposition(std::make_unique<Composition>());
    Layer *precomp = AddPrecompLayer(document, main->id, inner->id);
    precomp->transform.position.setStaticValue(Vec2{100, 0});

    Expected<SceneState, std::string> result = SceneEvaluator::Evaluate(document, main->id, 0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers.size(), 2u);
    const EvaluatedLayer *precompEval = FindEvaluated(*result, precomp->id);
    const EvaluatedLayer *rectEval = FindEvaluated(*result, rectLayer->id);
    ASSERT_NE(precompEval, nullptr);
    ASSERT_NE(rectEval, nullptr);
    EXPECT_EQ(rectEval->parentId, precomp->id);
    EXPECT_EQ(rectEval->worldTransform, Mat3::Translate(Vec2{100, 0}));
    EXPECT_EQ(rectEval->shapeItems[0].geometry.center, (Vec2{0, 0}));
    EXPECT_EQ(rectEval->shapeItems[0].geometry.size, (Vec2{10, 10}));
}

TEST(PrecompTest, TimeMappingAppliesStretchAndStart) {
    // innerTime = (outer - inPoint) * timeStretch + startTime
    Document document;
    Composition *inner = document.addComposition(std::make_unique<Composition>());
    Layer *rectLayer = AddRectLayer(document, inner->id, Vec2{0, 0});
    Keyframe<Vec2> from;
    from.time = 0;
    from.value = Vec2{0, 0};
    Keyframe<Vec2> to;
    to.time = 20;
    to.value = Vec2{100, 0};
    rectLayer->transform.position.addKeyframe(from);
    rectLayer->transform.position.addKeyframe(to);

    Composition *main = document.addComposition(std::make_unique<Composition>());
    Layer *precomp = AddPrecompLayer(document, main->id, inner->id);
    precomp->inPoint = 10;
    precomp->startTime = 5;
    precomp->timeStretch = 2;

    // outer 15 -> inner (15-10)*2+5 = 15 -> layer position x = 75; path stays local.
    Expected<SceneState, std::string> result = SceneEvaluator::Evaluate(document, main->id, 15);
    ASSERT_TRUE(result.hasValue());
    const EvaluatedLayer *rectEval = FindEvaluated(*result, rectLayer->id);
    ASSERT_NE(rectEval, nullptr);
    EXPECT_EQ(rectEval->worldTransform, Mat3::Translate(Vec2{75, 0}));
    EXPECT_EQ(rectEval->shapeItems[0].geometry.center, (Vec2{0, 0}));
}

TEST(PrecompTest, ThreeLevelNestingComposesTransformsAndOpacity) {
    Document document;
    Composition *deep = document.addComposition(std::make_unique<Composition>());
    AddRectLayer(document, deep->id, Vec2{0, 0});

    Composition *mid = document.addComposition(std::make_unique<Composition>());
    Layer *midPrecomp = AddPrecompLayer(document, mid->id, deep->id);
    midPrecomp->transform.position.setStaticValue(Vec2{10, 0});
    midPrecomp->transform.opacity.setStaticValue(0.5f);

    Composition *main = document.addComposition(std::make_unique<Composition>());
    Layer *mainPrecomp = AddPrecompLayer(document, main->id, mid->id);
    mainPrecomp->transform.position.setStaticValue(Vec2{20, 0});
    mainPrecomp->transform.opacity.setStaticValue(0.5f);

    Expected<SceneState, std::string> result = SceneEvaluator::Evaluate(document, main->id, 0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers.size(), 3u);
    const EvaluatedLayer *leaf = nullptr;
    for (const EvaluatedLayer &layer : result->layers) {
        if (!layer.shapeItems.empty()) {
            leaf = &layer;
            break;
        }
    }
    ASSERT_NE(leaf, nullptr);
    EXPECT_EQ(leaf->worldTransform, Mat3::Translate(Vec2{30, 0}));
    EXPECT_EQ(leaf->shapeItems[0].geometry.center, (Vec2{0, 0}));
    EXPECT_FLOAT_EQ(leaf->opacity, 0.25f);
}

TEST(PrecompTest, MissingTargetProducesNoLayers) {
    Document document;
    Composition *main = document.addComposition(std::make_unique<Composition>());
    AddPrecompLayer(document, main->id, EntityId{999});

    Expected<SceneState, std::string> result = SceneEvaluator::Evaluate(document, main->id, 0);
    ASSERT_TRUE(result.hasValue());
    EXPECT_TRUE(result->layers.empty());
}

TEST(PrecompTest, SelfReferencingCycleTerminates) {
    Document document;
    Composition *loop = document.addComposition(std::make_unique<Composition>());
    AddPrecompLayer(document, loop->id, loop->id);

    Expected<SceneState, std::string> result = SceneEvaluator::Evaluate(document, loop->id, 0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers.size(), 1u);
    EXPECT_TRUE(result->layers[0].shapeItems.empty());
}

TEST(PrecompTest, TwoCompositionCycleTerminates) {
    Document document;
    Composition *a = document.addComposition(std::make_unique<Composition>());
    Composition *b = document.addComposition(std::make_unique<Composition>());
    AddPrecompLayer(document, a->id, b->id);
    AddPrecompLayer(document, b->id, a->id);

    Expected<SceneState, std::string> result = SceneEvaluator::Evaluate(document, a->id, 0);
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result->layers.size(), 2u);
    EXPECT_TRUE(result->layers[0].shapeItems.empty());
    EXPECT_TRUE(result->layers[1].shapeItems.empty());
}
