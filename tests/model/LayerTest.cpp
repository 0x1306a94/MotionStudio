#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/model/Document.h"

using motion::ApproxEqual;
using motion::Composition;
using motion::Document;
using motion::EntityId;
using motion::Layer;
using motion::LayerType;
using motion::Mat3;
using motion::Vec2;

namespace {

struct TwoLayerScene {
    Document document;
    Composition* composition;
    Layer* parentLayer;
    Layer* childLayer;

    TwoLayerScene() {
        auto compositionPtr = std::make_unique<Composition>();
        composition = document.addComposition(std::move(compositionPtr));
        parentLayer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Null));
        childLayer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    }
};

}  // namespace

TEST(SetParentTest, AcceptsValidChain) {
    TwoLayerScene scene;
    EXPECT_TRUE(scene.childLayer->setParent(scene.parentLayer->id, scene.document));
    EXPECT_EQ(scene.childLayer->parentId, scene.parentLayer->id);
}

TEST(SetParentTest, RejectsSelfCycle) {
    TwoLayerScene scene;
    EXPECT_FALSE(scene.childLayer->setParent(scene.childLayer->id, scene.document));
    EXPECT_FALSE(scene.childLayer->parentId.isValid());
}

TEST(SetParentTest, RejectsIndirectCycle) {
    TwoLayerScene scene;
    Layer* grandchild =
        scene.document.addLayer(scene.composition->id, std::make_unique<Layer>(LayerType::Null));
    ASSERT_TRUE(scene.childLayer->setParent(scene.parentLayer->id, scene.document));
    ASSERT_TRUE(grandchild->setParent(scene.childLayer->id, scene.document));

    // parent → child → grandchild chain exists; making parent's parent = grandchild would create a cycle.
    EXPECT_FALSE(scene.parentLayer->setParent(grandchild->id, scene.document));
    EXPECT_FALSE(scene.parentLayer->parentId.isValid());
}

TEST(SetParentTest, RejectsDanglingParent) {
    TwoLayerScene scene;
    EXPECT_FALSE(scene.childLayer->setParent(EntityId{999}, scene.document));
}

TEST(SetParentTest, ClearingParentAlwaysSucceeds) {
    TwoLayerScene scene;
    ASSERT_TRUE(scene.childLayer->setParent(scene.parentLayer->id, scene.document));
    EXPECT_TRUE(scene.childLayer->setParent(EntityId{}, scene.document));
    EXPECT_FALSE(scene.childLayer->parentId.isValid());
}

TEST(LayerTransformTest, LocalComposesTranslateRotateScaleAnchor) {
    Document document;
    Composition* composition =
        document.addComposition(std::make_unique<Composition>());
    Layer* layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));

    layer->transform.position.setStaticValue({10, 20});
    layer->transform.scale.setStaticValue({2, 2});
    layer->transform.anchorPoint.setStaticValue({1, 1});

    Mat3 local = layer->localTransform(0);
    Vec2 transformed = local.transformPoint({2, 2});
    EXPECT_EQ(transformed, (Vec2{12, 22}));
}

TEST(LayerTransformTest, WorldAppliesParentChain) {
    TwoLayerScene scene;
    ASSERT_TRUE(scene.childLayer->setParent(scene.parentLayer->id, scene.document));

    scene.parentLayer->transform.position.setStaticValue({100, 0});
    scene.childLayer->transform.position.setStaticValue({0, 50});

    Mat3 world = scene.childLayer->worldTransform(0, scene.document);
    EXPECT_TRUE(ApproxEqual(world.transformPoint({0, 0}), Vec2{100, 50}));
}

TEST(LayerTransformTest, WorldFallsBackToLocalWithDanglingParent) {
    TwoLayerScene scene;
    // Bypass setParent and write a dangling parent ID directly (simulate corrupt file).
    scene.childLayer->parentId = EntityId{999};
    scene.childLayer->transform.position.setStaticValue({5, 5});

    Mat3 world = scene.childLayer->worldTransform(0, scene.document);
    EXPECT_TRUE(ApproxEqual(world.transformPoint({0, 0}), Vec2{5, 5}));
}

TEST(LayerTest, ConstructorCreatesMatchingContent) {
    Layer shapeLayer{LayerType::Shape};
    EXPECT_EQ(shapeLayer.type(), LayerType::Shape);
    ASSERT_NE(shapeLayer.content, nullptr);
    EXPECT_EQ(shapeLayer.content->type(), motion::LayerType::Shape);

    Layer precompLayer{LayerType::Precomp};
    EXPECT_EQ(precompLayer.content->type(), motion::LayerType::Precomp);
}
