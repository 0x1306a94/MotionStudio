#include <gtest/gtest.h>

#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/VectorNetworkConvert.h"
#include "MotionStudio/render/PathEditHandles.h"
#include "MotionStudio/render/ShapeGeometry.h"

using motion::BezierPath;
using motion::BezierPathToVectorNetwork;
using motion::BuildPathEditCommands;
using motion::BuildPathEditHandles;
using motion::BuildPathEditHandlesFromNetwork;
using motion::BuildPathEditHandlesFromPath;
using motion::DrawCommandType;
using motion::EntityId;
using motion::EvaluatedLayer;
using motion::EvaluatedMask;
using motion::EvaluatedShapeItem;
using motion::HitTestPathEdit;
using motion::MakePathGeometry;
using motion::MakeSingleContour;
using motion::Mat3;
using motion::PathEditHandles;
using motion::PathEditHit;
using motion::PathEditKind;
using motion::PathEditTarget;
using motion::PathHandleKind;
using motion::SceneState;
using motion::Vec2;
using motion::VectorNetwork;

namespace {

BezierPath MakeTriangle() {
    BezierPath path = MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{0, 10}, {}, {}}}, true);
    return path;
}

BezierPath MakeOpenLine() {
    BezierPath path = MakeSingleContour({{{0, 0}, {}, {2, 0}}, {{10, 0}, {-2, 0}, {}}}, false);
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
    layer.shapeNetwork = BezierPathToVectorNetwork(MakeTriangle());
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
    EXPECT_EQ(handles.localNetwork.vertices.size(), 3u);
    EXPECT_EQ(handles.localNetwork.edges.size(), 3u);
    EXPECT_FLOAT_EQ(handles.worldVertices[0].x, 5);
    EXPECT_FLOAT_EQ(handles.worldVertices[0].y, 7);
}

TEST(PathEditHandlesTest, HitTestSharedVertexEdgeTangent) {
    // Hub with three spokes: degree 3 at center → EdgeTangent, no mirroring pair.
    VectorNetwork network;
    network.vertices = {{1, {0, 0}}, {2, {20, 0}}, {3, {0, 20}}, {4, {-20, 0}}};
    network.edges = {
        {1, 1, 2, {8, 0}, {-8, 0}},
        {2, 1, 3, {0, 8}, {0, -8}},
        {3, 1, 4, {-8, 0}, {8, 0}},
    };

    PathEditHandles handles;
    ASSERT_TRUE(BuildPathEditHandlesFromNetwork(network, Mat3::Identity(), {}, 0, handles));

    PathEditHit hit = HitTestPathEdit(handles, {8, 0}, 3.0f, 2.0f);
    EXPECT_EQ(hit.kind, PathHandleKind::EdgeTangent);
    EXPECT_EQ(hit.edgeId, 1u);
    EXPECT_TRUE(hit.atStart);
}

TEST(PathEditHandlesTest, HitTestFindsSegment) {
    PathEditHandles handles;
    ASSERT_TRUE(BuildPathEditHandlesFromPath(MakeTriangle(), Mat3::Identity(), {}, -1, handles));

    PathEditHit hit = HitTestPathEdit(handles, {5, 0}, 1.0f, 3.0f);
    EXPECT_EQ(hit.kind, PathHandleKind::Segment);
    EXPECT_EQ(hit.index, 0u);
    EXPECT_EQ(hit.edgeId, 1u);
    EXPECT_NEAR(hit.segmentT, 0.5f, 0.1f);
}

TEST(PathEditHandlesTest, HitTestPrefersTangentOverVertex) {
    // Closed triangle keeps degree==2 so In/OutTangent chrome stays available.
    BezierPath path =
        MakeSingleContour({{{0, 0}, {0, -10}, {0, 10}}, {{20, 0}, {}, {}}, {{0, 20}, {}, {}}}, true);
    PathEditHandles handles;
    ASSERT_TRUE(BuildPathEditHandlesFromPath(path, Mat3::Identity(), {}, 0, handles));

    PathEditHit hit = HitTestPathEdit(handles, handles.worldOutHandles[0], 3.0f, 2.0f);
    EXPECT_EQ(hit.kind, PathHandleKind::OutTangent);
    EXPECT_EQ(hit.index, 0u);
    EXPECT_NE(hit.edgeId, 0u);
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

TEST(PathEditHandlesTest, VertexZoneBeatsNearbySegment) {
    PathEditHandles handles;
    ASSERT_TRUE(BuildPathEditHandlesFromPath(MakeTriangle(), Mat3::Identity(), {}, -1, handles));

    // Slightly off the vertex toward the edge: still inside the vertex zone.
    PathEditHit hit = HitTestPathEdit(handles, {1.5f, 0}, 4.0f, 6.0f);
    EXPECT_EQ(hit.kind, PathHandleKind::Vertex);
    EXPECT_EQ(hit.index, 0u);
}

TEST(PathEditHandlesTest, BuildCommandsNonEmpty) {
    PathEditHandles handles;
    ASSERT_TRUE(BuildPathEditHandlesFromPath(MakeTriangle(), Mat3::Identity(), {}, 1, handles));
    auto commands = BuildPathEditCommands(handles, 1.5f, 7.0f);
    EXPECT_FALSE(commands.empty());
}

TEST(PathEditHandlesTest, SingleVertexSkipsPathStrokeButDrawsMarker) {
    BezierPath path = MakeSingleContour({{{3, 4}, {}, {}}}, false);
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
