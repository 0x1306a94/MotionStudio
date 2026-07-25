#include <gtest/gtest.h>

#include "MotionStudio/render/HitTest.h"

using motion::BezierPath;
using motion::EntityId;
using motion::EvaluatedLayer;
using motion::EvaluatedShapeItem;
using motion::HitTestLayer;
using motion::HitTestLayerAtPoint;
using motion::Paint;
using motion::SceneState;
using motion::Vec2;

namespace {

BezierPath RectPath(float left, float top, float right, float bottom) {
    BezierPath path;
    path.closed = true;
    path.vertices.push_back({{left, top}, {}, {}});
    path.vertices.push_back({{right, top}, {}, {}});
    path.vertices.push_back({{right, bottom}, {}, {}});
    path.vertices.push_back({{left, bottom}, {}, {}});
    return path;
}

EvaluatedLayer LayerWithRect(uint64_t id, float left, float top, float right, float bottom) {
    EvaluatedLayer layer;
    layer.id = EntityId{id};
    EvaluatedShapeItem item;
    item.path = RectPath(left, top, right, bottom);
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
    item.path.closed = false;
    item.path.vertices.push_back({{0, 0}, {}, {}});
    item.path.vertices.push_back({{100, 0}, {}, {}});
    layer.shapeItems.push_back(item);

    EXPECT_TRUE(HitTestLayer(layer, Vec2{50, 4}, 2));
    EXPECT_FALSE(HitTestLayer(layer, Vec2{50, 8}, 1));
}
