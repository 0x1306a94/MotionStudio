#include <gtest/gtest.h>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/MaskMode.h"
#include "MotionStudio/render/DrawCommand.h"
#include "MotionStudio/render/EvaluatedLayer.h"
#include "MotionStudio/render/PathOverlay.h"
#include "MotionStudio/render/SceneState.h"
#include "MotionStudio/render/ShapeGeometry.h"

using motion::BezierPath;
using motion::BuildPathOverlayCommands;
using motion::CollectMaskPathOverlays;
using motion::Color;
using motion::DrawCommandType;
using motion::EntityId;
using motion::EvaluatedLayer;
using motion::EvaluatedMask;
using motion::MaskMode;
using motion::Mat3;
using motion::PathOverlayItem;
using motion::SceneState;
using motion::ShapeGeometryKind;
using motion::Vec2;

namespace {

BezierPath UnitSquare() {
    BezierPath path;
    path.closed = true;
    path.vertices.push_back({{0, 0}, {}, {}});
    path.vertices.push_back({{100, 0}, {}, {}});
    path.vertices.push_back({{100, 100}, {}, {}});
    path.vertices.push_back({{0, 100}, {}, {}});
    return path;
}

}  // namespace

TEST(PathOverlayTest, BuildCommandsEmptyWhenNoItems) {
    EXPECT_TRUE(BuildPathOverlayCommands({}, 1.5f).empty());
}

TEST(PathOverlayTest, BuildCommandsSkipsSingleVertexPath) {
    PathOverlayItem item;
    item.path.vertices.push_back({{0, 0}, {}, {}});
    item.color = Color{1, 0.85f, 0.2f, 1};
    EXPECT_TRUE(BuildPathOverlayCommands({item}, 1.5f).empty());
}

TEST(PathOverlayTest, BuildCommandsEmitsStrokeInLocalTransform) {
    PathOverlayItem item;
    item.worldTransform = Mat3::Translate(Vec2{10, 20});
    item.path = UnitSquare();
    item.color = Color{1, 0.85f, 0.2f, 1};

    auto commands = BuildPathOverlayCommands({item}, 1.5f);
    ASSERT_EQ(commands.size(), 4u);
    EXPECT_EQ(commands[0].type, DrawCommandType::Save);
    EXPECT_EQ(commands[1].type, DrawCommandType::ConcatTransform);
    EXPECT_EQ(commands[1].transform, item.worldTransform);
    EXPECT_EQ(commands[2].type, DrawCommandType::StrokePath);
    EXPECT_EQ(commands[2].paint.color, item.color);
    EXPECT_FLOAT_EQ(commands[2].stroke.width, 1.5f);
    EXPECT_EQ(commands[2].geometry.kind, ShapeGeometryKind::Path);
    EXPECT_EQ(commands[2].geometry.path, item.path);
    EXPECT_EQ(commands[3].type, DrawCommandType::Restore);
}

TEST(PathOverlayTest, CollectMaskPathOverlaysForSelectedLayer) {
    SceneState state;
    EvaluatedLayer layer;
    layer.id = EntityId{42};
    layer.worldTransform = Mat3::Translate(Vec2{5, 7});
    EvaluatedMask mask;
    mask.path = UnitSquare();
    mask.mode = MaskMode::Add;
    layer.masks.push_back(mask);
    state.layers.push_back(std::move(layer));

    const Color color{1, 0.85f, 0.2f, 1};
    auto items = CollectMaskPathOverlays(state, {EntityId{42}}, color);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].path, UnitSquare());
    EXPECT_EQ(items[0].worldTransform, Mat3::Translate(Vec2{5, 7}));
    EXPECT_EQ(items[0].color, color);
}

TEST(PathOverlayTest, CollectMaskPathOverlaysSkipsUnselected) {
    SceneState state;
    EvaluatedLayer layer;
    layer.id = EntityId{42};
    EvaluatedMask mask;
    mask.path = UnitSquare();
    layer.masks.push_back(mask);
    state.layers.push_back(std::move(layer));

    EXPECT_TRUE(CollectMaskPathOverlays(state, {EntityId{7}}, Color{}).empty());
    EXPECT_TRUE(CollectMaskPathOverlays(state, {}, Color{}).empty());
}
