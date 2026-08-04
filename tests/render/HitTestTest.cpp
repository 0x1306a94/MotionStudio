#include <gtest/gtest.h>

#include "MotionStudio/render/EvaluatedTextItem.h"
#include "MotionStudio/render/HitTest.h"
#include "MotionStudio/render/ShapeGeometry.h"

using motion::BezierPath;
using motion::BoundsOfLayerLocal;
using motion::EntityId;
using motion::EvaluatedLayer;
using motion::EvaluatedShapeItem;
using motion::EvaluatedTextItem;
using motion::HitTestLayer;
using motion::HitTestLayerAtPoint;
using motion::MakePathGeometry;
using motion::MakeRectGeometry;
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
    BezierPath path;
    path.closed = false;
    path.vertices.push_back({{0, 0}, {}, {}});
    path.vertices.push_back({{100, 0}, {}, {}});
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
