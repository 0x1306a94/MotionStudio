#include <gtest/gtest.h>

#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/render/SelectionHandles.h"
#include "MotionStudio/render/ShapeGeometry.h"

using motion::BezierPath;
using motion::BuildSelectionHandleCommands;
using motion::BuildSelectionHandles;
using motion::Color;
using motion::DrawCommandType;
using motion::EntityId;
using motion::EvaluatedLayer;
using motion::EvaluatedShapeItem;
using motion::HitTestSelectionHandle;
using motion::MakePathGeometry;
using motion::Mat3;
using motion::Paint;
using motion::SceneState;
using motion::SelectionHandleKind;
using motion::SelectionHandles;
using motion::ShapeGeometryKind;

namespace {

EvaluatedShapeItem MakeUnitFill() {
    EvaluatedShapeItem item;
    BezierPath path;
    path.closed = true;
    path.vertices.push_back({{0, 0}, {}, {}});
    path.vertices.push_back({{10, 0}, {}, {}});
    path.vertices.push_back({{10, 10}, {}, {}});
    path.vertices.push_back({{0, 10}, {}, {}});
    item.geometry = MakePathGeometry(std::move(path));
    item.paint = Paint{Color{1, 0, 0, 1}, motion::FillRule::NonZero};
    return item;
}

EvaluatedLayer MakeLayer(EntityId id, Mat3 world) {
    EvaluatedLayer layer;
    layer.id = id;
    layer.worldTransform = world;
    layer.worldAnchor = world.transformPoint({0, 0});
    layer.shapeItems.push_back(MakeUnitFill());
    return layer;
}

}  // namespace

TEST(SelectionHandlesTest, SingleLayerBuildsOrientedBox) {
    SceneState state;
    state.layers.push_back(MakeLayer(EntityId{1}, Mat3::Rotate(90) * Mat3::Translate({5, 5})));

    SelectionHandles handles;
    ASSERT_TRUE(BuildSelectionHandles(state, {EntityId{1}}, EntityId{1}, handles));
    EXPECT_TRUE(handles.isOriented);
    EXPECT_EQ(handles.primaryLayerId, EntityId{1});
    EXPECT_FLOAT_EQ(handles.localMin.x, 0);
    EXPECT_FLOAT_EQ(handles.localMax.x, 10);
}

TEST(SelectionHandlesTest, MultiSelectBuildsAxisAlignedUnion) {
    SceneState state;
    state.layers.push_back(MakeLayer(EntityId{1}, Mat3::Identity()));
    state.layers.push_back(MakeLayer(EntityId{2}, Mat3::Translate({20, 0})));

    SelectionHandles handles;
    ASSERT_TRUE(BuildSelectionHandles(state, {EntityId{1}, EntityId{2}}, EntityId{2}, handles));
    EXPECT_FALSE(handles.isOriented);
    EXPECT_FLOAT_EQ(handles.corners[0].x, 0);
    EXPECT_FLOAT_EQ(handles.corners[2].x, 30);
    EXPECT_EQ(handles.primaryLayerId, EntityId{2});
}

TEST(SelectionHandlesTest, HitTestPrefersAnchorOverScale) {
    SceneState state;
    state.layers.push_back(MakeLayer(EntityId{1}, Mat3::Identity()));
    SelectionHandles handles;
    ASSERT_TRUE(BuildSelectionHandles(state, {EntityId{1}}, EntityId{1}, handles));
    handles.anchor = handles.corners[0];

    EXPECT_EQ(HitTestSelectionHandle(handles, handles.anchor, 4, 6, 18),
              SelectionHandleKind::Anchor);
}

TEST(SelectionHandlesTest, CommandsIncludeBoxAndHandles) {
    SelectionHandles handles;
    handles.valid = true;
    handles.corners[0] = {0, 0};
    handles.corners[1] = {10, 0};
    handles.corners[2] = {10, 10};
    handles.corners[3] = {0, 10};
    handles.edgeMids[0] = {5, 0};
    handles.edgeMids[1] = {10, 5};
    handles.edgeMids[2] = {5, 10};
    handles.edgeMids[3] = {0, 5};
    handles.center = {5, 5};
    handles.anchor = {2, 2};

    auto commands = BuildSelectionHandleCommands(handles, 1.5f, 7.0f);
    ASSERT_FALSE(commands.empty());
    EXPECT_EQ(commands.front().type, DrawCommandType::StrokePath);
    EXPECT_EQ(commands.front().geometry.kind, ShapeGeometryKind::Path);

    auto withoutAnchor = BuildSelectionHandleCommands(handles, 1.5f, 7.0f, false);
    EXPECT_LT(withoutAnchor.size(), commands.size());
}
