#include <gtest/gtest.h>

#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/render/PathEditHandles.h"
#include "MotionStudio/render/ShapeGeometry.h"

using motion::BezierPath;
using motion::BuildPathEditCommands;
using motion::BuildPathEditHandles;
using motion::BuildPathEditHandlesFromPath;
using motion::DrawCommandType;
using motion::EntityId;
using motion::EvaluatedLayer;
using motion::EvaluatedMask;
using motion::EvaluatedShapeItem;
using motion::HitTestPathEdit;
using motion::MakePathGeometry;
using motion::Mat3;
using motion::PathEditHandles;
using motion::PathEditHit;
using motion::PathEditKind;
using motion::PathEditTarget;
using motion::PathHandleKind;
using motion::SceneState;
using motion::Vec2;

namespace {

BezierPath MakeTriangle() {
    BezierPath path;
    path.closed = true;
    path.vertices.push_back({{0, 0}, {}, {}});
    path.vertices.push_back({{10, 0}, {}, {}});
    path.vertices.push_back({{0, 10}, {}, {}});
    return path;
}

BezierPath MakeOpenLine() {
    BezierPath path;
    path.vertices.push_back({{0, 0}, {}, {2, 0}});
    path.vertices.push_back({{10, 0}, {-2, 0}, {}});
    return path;
}

}  // namespace

TEST(PathEditHandlesTest, EmptyPathIsInvalid) {
    PathEditHandles handles;
    EXPECT_FALSE(BuildPathEditHandlesFromPath({}, Mat3::Identity(), {}, -1, handles));
    EXPECT_FALSE(handles.valid);
    EXPECT_TRUE(BuildPathEditCommands(handles, 1.5f, 7.0f).empty());
}

TEST(PathEditHandlesTest, BuildFromSceneShape) {
    SceneState state;
    EvaluatedLayer layer;
    layer.id = EntityId{1};
    layer.worldTransform = Mat3::Translate({5, 7});
    EvaluatedShapeItem item;
    item.geometry = MakePathGeometry(MakeTriangle());
    layer.shapeItems.push_back(std::move(item));
    state.layers.push_back(std::move(layer));

    PathEditTarget target;
    target.kind = PathEditKind::Shape;
    target.layerId = EntityId{1};

    PathEditHandles handles;
    ASSERT_TRUE(BuildPathEditHandles(state, target, -1, handles));
    EXPECT_TRUE(handles.valid);
    ASSERT_EQ(handles.worldVertices.size(), 3u);
    EXPECT_FLOAT_EQ(handles.worldVertices[0].x, 5);
    EXPECT_FLOAT_EQ(handles.worldVertices[0].y, 7);
}

TEST(PathEditHandlesTest, HitTestPrefersTangentOverVertex) {
    BezierPath path;
    path.vertices.push_back({{0, 0}, {0, -10}, {0, 10}});
    path.vertices.push_back({{20, 0}, {}, {}});
    PathEditHandles handles;
    ASSERT_TRUE(BuildPathEditHandlesFromPath(path, Mat3::Identity(), {}, 0, handles));

    // Out handle sits on the vertex; tangent hit must win over Vertex.
    PathEditHit hit = HitTestPathEdit(handles, handles.worldOutHandles[0], 3.0f, 2.0f);
    EXPECT_EQ(hit.kind, PathHandleKind::OutTangent);
    EXPECT_EQ(hit.index, 0u);
}

TEST(PathEditHandlesTest, HitTestPrefersVertexOverSegment) {
    PathEditHandles handles;
    ASSERT_TRUE(BuildPathEditHandlesFromPath(MakeTriangle(), Mat3::Identity(), {}, -1, handles));

    PathEditHit hit = HitTestPathEdit(handles, {10, 0}, 4.0f, 3.0f);
    EXPECT_EQ(hit.kind, PathHandleKind::Vertex);
    EXPECT_EQ(hit.index, 1u);
}

TEST(PathEditHandlesTest, OpenPathFirstVertexIsCloseRing) {
    PathEditHandles handles;
    ASSERT_TRUE(BuildPathEditHandlesFromPath(MakeOpenLine(), Mat3::Identity(), {}, -1, handles));

    PathEditHit hit = HitTestPathEdit(handles, {0, 0}, 4.0f, 3.0f);
    EXPECT_EQ(hit.kind, PathHandleKind::CloseRing);
    EXPECT_EQ(hit.index, 0u);
}

TEST(PathEditHandlesTest, HitTestFindsSegment) {
    PathEditHandles handles;
    ASSERT_TRUE(BuildPathEditHandlesFromPath(MakeTriangle(), Mat3::Identity(), {}, -1, handles));

    PathEditHit hit = HitTestPathEdit(handles, {5, 0}, 1.0f, 3.0f);
    EXPECT_EQ(hit.kind, PathHandleKind::Segment);
    EXPECT_EQ(hit.index, 0u);
    EXPECT_NEAR(hit.segmentT, 0.5f, 0.1f);
}

TEST(PathEditHandlesTest, MidSegmentWinsOverLargeVertexRadius) {
    PathEditHandles handles;
    ASSERT_TRUE(BuildPathEditHandlesFromPath(MakeTriangle(), Mat3::Identity(), {}, -1, handles));

    // Midpoint of edge (0,0)-(10,0). A 14-unit vertex radius covers this point,
    // but segment distance is nearer so insert-on-edge must still win.
    PathEditHit hit = HitTestPathEdit(handles, {5, 0}, 14.0f, 6.0f);
    EXPECT_EQ(hit.kind, PathHandleKind::Segment);
    EXPECT_EQ(hit.index, 0u);
}

TEST(PathEditHandlesTest, BuildCommandsNonEmpty) {
    PathEditHandles handles;
    ASSERT_TRUE(BuildPathEditHandlesFromPath(MakeTriangle(), Mat3::Identity(), {}, 1, handles));
    auto commands = BuildPathEditCommands(handles, 1.5f, 7.0f);
    EXPECT_FALSE(commands.empty());
}

TEST(PathEditHandlesTest, SingleVertexSkipsPathStrokeButDrawsMarker) {
    BezierPath path;
    path.vertices.push_back({{3, 4}, {}, {}});
    PathEditHandles handles;
    ASSERT_TRUE(BuildPathEditHandlesFromPath(path, Mat3::Identity(), {}, 0, handles));
    auto commands = BuildPathEditCommands(handles, 1.5f, 7.0f);
    ASSERT_FALSE(commands.empty());
    // Overlay stroke is skipped; only vertex marker fill/stroke remain.
    bool sawFill = false;
    for (const auto &command : commands) {
        EXPECT_NE(command.type, DrawCommandType::Save);
        EXPECT_NE(command.type, DrawCommandType::ConcatTransform);
        if (command.type == DrawCommandType::DrawPath) {
            sawFill = true;
        }
    }
    EXPECT_TRUE(sawFill);
}
