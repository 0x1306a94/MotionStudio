#include <gtest/gtest.h>

#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/render/EvaluatedImageItem.h"
#include "MotionStudio/render/EvaluatedTextItem.h"
#include "MotionStudio/render/HitTest.h"
#include "MotionStudio/render/ShapeGeometry.h"

using motion::BezierPath;
using motion::BoundsOfDescendantUnionLocal;
using motion::BoundsOfLayerIncludingDescendants;
using motion::BoundsOfLayerLocal;
using motion::EntityId;
using motion::EvaluatedImageItem;
using motion::EvaluatedLayer;
using motion::EvaluatedShapeItem;
using motion::EvaluatedTextItem;
using motion::HitTestLayer;
using motion::HitTestLayerAtPoint;
using motion::MakePathGeometry;
using motion::MakeRectGeometry;
using motion::MakeSingleContour;
using motion::Mat3;
using motion::Paint;
using motion::SceneState;
using motion::Vec2;

namespace {

EvaluatedLayer LayerWithRect(uint64_t id, float left, float top, float right, float bottom) {
    EvaluatedLayer layer;
    layer.id = EntityId{id};
    EvaluatedShapeItem item;
    const Vec2 center{(left + right) * 0.5f, (top + bottom) * 0.5f};
    const Vec2 size{right - left, bottom - top};
    item.geometry = MakeRectGeometry(center, size);
    item.paint = Paint{{1, 1, 1, 1}};
    layer.shapeItems.push_back(item);
    return layer;
}

}  // namespace

TEST(HitTestTest, HitsFilledShapeInterior) {
    const EvaluatedLayer layer = LayerWithRect(1, 10, 20, 110, 120);

    EXPECT_TRUE(HitTestLayer(layer, Vec2{60, 70}, 0));
    EXPECT_FALSE(HitTestLayer(layer, Vec2{5, 70}, 0));
}

TEST(HitTestTest, ReturnsTopmostLayer) {
    SceneState state;
    state.layers.push_back(LayerWithRect(1, 0, 0, 100, 100));
    state.layers.push_back(LayerWithRect(2, 25, 25, 75, 75));

    EXPECT_EQ(HitTestLayerAtPoint(state, Vec2{50, 50}, 0).value, 2);
    EXPECT_EQ(HitTestLayerAtPoint(state, Vec2{10, 10}, 0).value, 1);
}

TEST(HitTestTest, HitsStrokeWithinTolerance) {
    EvaluatedLayer layer;
    layer.id = EntityId{1};
    EvaluatedShapeItem item;
    item.isStroke = true;
    item.stroke.width = 4;
    BezierPath path = MakeSingleContour({{{0, 0}, {}, {}}, {{100, 0}, {}, {}}}, false);
    item.geometry = MakePathGeometry(std::move(path));
    layer.shapeItems.push_back(item);

    EXPECT_TRUE(HitTestLayer(layer, Vec2{50, 4}, 2));
    EXPECT_FALSE(HitTestLayer(layer, Vec2{50, 8}, 1));
}

TEST(HitTestTest, TextExactLocalBoundsNotOriginAnchored) {
    EvaluatedLayer layer;
    layer.id = EntityId{1};
    EvaluatedTextItem text;
    text.containerSize = {40, 20};
    text.useExactLocalBounds = true;
    text.localBoundsMin = {-10.0f, -30.0f};
    text.localBoundsMax = {50.0f, 10.0f};
    layer.textItem = text;

    Vec2 minPoint;
    Vec2 maxPoint;
    ASSERT_TRUE(BoundsOfLayerLocal(layer, minPoint, maxPoint));
    EXPECT_FLOAT_EQ(minPoint.x, -10.0f);
    EXPECT_FLOAT_EQ(minPoint.y, -30.0f);
    EXPECT_FLOAT_EQ(maxPoint.x, 50.0f);
    EXPECT_FLOAT_EQ(maxPoint.y, 10.0f);
    EXPECT_TRUE(HitTestLayer(layer, {-5.0f, -10.0f}, 0));
    EXPECT_FALSE(HitTestLayer(layer, {5.0f, 20.0f}, 0));
}

TEST(HitTestTest, DescendantUnionLocalForGroup) {
    SceneState state;
    EvaluatedLayer group;
    group.id = EntityId{1};
    group.worldTransform = Mat3::Translate({10, 0});
    group.worldAnchor = {10, 0};

    EvaluatedLayer child = LayerWithRect(2, 0, 0, 10, 10);
    child.parentId = EntityId{1};
    child.worldTransform = Mat3::Translate({10, 0});
    state.layers.push_back(group);
    state.layers.push_back(child);

    Vec2 localMin;
    Vec2 localMax;
    ASSERT_TRUE(BoundsOfDescendantUnionLocal(state, EntityId{1}, localMin, localMax));
    EXPECT_FLOAT_EQ(localMin.x, 0);
    EXPECT_FLOAT_EQ(localMin.y, 0);
    EXPECT_FLOAT_EQ(localMax.x, 10);
    EXPECT_FLOAT_EQ(localMax.y, 10);

    Vec2 sceneMin;
    Vec2 sceneMax;
    ASSERT_TRUE(BoundsOfLayerIncludingDescendants(state, group, sceneMin, sceneMax));
    EXPECT_FLOAT_EQ(sceneMin.x, 10);
    EXPECT_FLOAT_EQ(sceneMin.y, 0);
    EXPECT_FLOAT_EQ(sceneMax.x, 20);
    EXPECT_FLOAT_EQ(sceneMax.y, 10);
}

TEST(HitTestTest, ImageCornerRadiusRejectsOutsideRound) {
    EvaluatedLayer layer;
    layer.opacity = 1.0f;
    EvaluatedImageItem image;
    image.containerSize = {100, 100};
    image.cornerRadius = 50.0f;
    layer.imageItem = image;
    EXPECT_TRUE(HitTestLayer(layer, {50, 50}, 0));
    EXPECT_FALSE(HitTestLayer(layer, {1, 1}, 0));
}

TEST(HitTestTest, GroupCornerRadiusClipsChildHit) {
    SceneState state;
    EvaluatedLayer child;
    child.id = EntityId{2};
    child.parentId = EntityId{1};
    child.opacity = 1.0f;
    child.worldTransform = Mat3::Identity();
    EvaluatedShapeItem item;
    item.geometry = MakeRectGeometry({50, 50}, {100, 100});
    item.paint = Paint{{1, 1, 1, 1}};
    child.shapeItems.push_back(item);
    EvaluatedLayer group;
    group.id = EntityId{1};
    group.opacity = 1.0f;
    group.cornerRadius = 50.0f;
    group.worldTransform = Mat3::Identity();
    state.layers.push_back(child);
    state.layers.push_back(group);
    EXPECT_EQ(HitTestLayerAtPoint(state, {50, 50}, 0).value, 2);
    EXPECT_EQ(HitTestLayerAtPoint(state, {1, 1}, 0).value, 0);
}
