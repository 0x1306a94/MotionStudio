#include <gtest/gtest.h>

#include <cmath>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/VectorNetworkConvert.h"
#include "MotionStudio/common/VectorNetworkEdit.h"

using motion::AddEdge;
using motion::AddVertex;
using motion::BezierPathToVectorNetwork;
using motion::FindEdge;
using motion::FindVertex;
using motion::InsertVertexOnEdge;
using motion::MakeSingleContour;
using motion::MoveEdgeTangent;
using motion::MoveVertex;
using motion::RecenterNetwork;
using motion::RemoveEdge;
using motion::RemoveVertex;
using motion::SetVertexMirrorMode;
using motion::ToggleVertexSmooth;
using motion::Vec2;
using motion::VectorNetwork;
using motion::VertexDegree;
using motion::VertexMirrorMode;

namespace {

VectorNetwork ClosedTriangle() {
    return BezierPathToVectorNetwork(
        MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{0, 10}, {}, {}}}, true));
}

VectorNetwork MakeDegreeTwoChain() {
    VectorNetwork network;
    network.vertices = {{1, {0, 0}}, {2, {10, 0}}, {3, {20, 0}}};
    network.edges = {{1, 1, 2, {}, {}}, {2, 2, 3, {}, {}}};
    return network;
}

Vec2 HandleAt(const VectorNetwork &network, uint32_t edgeId, uint32_t vertexId) {
    const VectorNetwork::Edge *edge = FindEdge(network, edgeId);
    if (edge == nullptr) {
        return {};
    }
    if (edge->start == vertexId) {
        return edge->startTangent;
    }
    if (edge->end == vertexId) {
        return edge->endTangent;
    }
    return {};
}

float HandleLength(Vec2 value) {
    return std::sqrt(value.x * value.x + value.y * value.y);
}

}  // namespace

TEST(VectorNetworkEditTest, AddEdgeFromClosedTriangleToNewPoint) {
    VectorNetwork network = ClosedTriangle();
    ASSERT_EQ(network.vertices.size(), 3u);
    ASSERT_EQ(VertexDegree(network, network.vertices[0].id), 2);

    uint32_t p4 = 0;
    network = AddVertex(network, {5, 5}, &p4);
    EXPECT_NE(p4, 0u);
    network = AddEdge(network, network.vertices[0].id, p4, nullptr);
    EXPECT_EQ(VertexDegree(network, network.vertices[0].id), 3);
    EXPECT_EQ(VertexDegree(network, p4), 1);
}

TEST(VectorNetworkEditTest, AddEdgeNoOpWhenUndirectedEdgeExists) {
    VectorNetwork network = ClosedTriangle();
    const uint32_t start = network.vertices[0].id;
    const uint32_t end = network.vertices[1].id;
    const size_t before = network.edges.size();
    uint32_t outId = 99;
    network = AddEdge(network, start, end, &outId);
    EXPECT_EQ(network.edges.size(), before);
    EXPECT_EQ(outId, 0u);
    // Reverse direction of an existing edge is also a no-op.
    outId = 99;
    network = AddEdge(network, end, start, &outId);
    EXPECT_EQ(network.edges.size(), before);
    EXPECT_EQ(outId, 0u);
}

TEST(VectorNetworkEditTest, MoveVertexUpdatesSharedPoint) {
    VectorNetwork network = ClosedTriangle();
    uint32_t hub = 0;
    network = AddVertex(network, {2, 2}, &hub);
    network = AddEdge(network, network.vertices[0].id, hub, nullptr);
    network = AddEdge(network, network.vertices[1].id, hub, nullptr);
    network = MoveVertex(network, hub, {4, 4});
    const VectorNetwork::Vertex *vertex = FindVertex(network, hub);
    ASSERT_NE(vertex, nullptr);
    EXPECT_FLOAT_EQ(vertex->point.x, 4.0f);
    EXPECT_FLOAT_EQ(vertex->point.y, 4.0f);
}

TEST(VectorNetworkEditTest, MirrorIgnoredWhenDegreeGreaterThanTwo) {
    VectorNetwork network = ClosedTriangle();
    uint32_t hub = network.vertices[0].id;
    uint32_t extra = 0;
    network = AddVertex(network, {5, 5}, &extra);
    network = AddEdge(network, hub, extra, nullptr);
    ASSERT_EQ(VertexDegree(network, hub), 3);

    // Find an original rim edge that starts or ends at hub.
    uint32_t edgeId = 0;
    bool atStart = true;
    for (const VectorNetwork::Edge &edge : network.edges) {
        if (edge.start == hub) {
            edgeId = edge.id;
            atStart = true;
            break;
        }
        if (edge.end == hub) {
            edgeId = edge.id;
            atStart = false;
            break;
        }
    }
    ASSERT_NE(edgeId, 0u);

    VectorNetwork before = network;
    network = MoveEdgeTangent(network, edgeId, atStart, {3, 0}, true);
    const VectorNetwork::Edge *edited = FindEdge(network, edgeId);
    ASSERT_NE(edited, nullptr);
    if (atStart) {
        EXPECT_FLOAT_EQ(edited->startTangent.x, 3.0f);
    } else {
        EXPECT_FLOAT_EQ(edited->endTangent.x, 3.0f);
    }
    // Other edges' handles at hub must be unchanged.
    for (size_t i = 0; i < network.edges.size(); ++i) {
        if (network.edges[i].id == edgeId) {
            continue;
        }
        EXPECT_EQ(network.edges[i].startTangent, before.edges[i].startTangent);
        EXPECT_EQ(network.edges[i].endTangent, before.edges[i].endTangent);
    }
}

TEST(VectorNetworkEditTest, MirrorWhenDegreeTwo) {
    VectorNetwork network =
        BezierPathToVectorNetwork(MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{20, 0}, {}, {}}}, false));
    ASSERT_EQ(network.edges.size(), 2u);
    const uint32_t mid = network.vertices[1].id;
    ASSERT_EQ(VertexDegree(network, mid), 2);
    network = SetVertexMirrorMode(network, mid, VertexMirrorMode::AngleLength);

    // Edge 0: v0→v1, Edge 1: v1→v2. Edit end handle of edge 0 (at mid).
    const uint32_t leftEdge = network.edges[0].id;
    const uint32_t rightEdge = network.edges[1].id;
    network = MoveEdgeTangent(network, leftEdge, false, {0, 4}, true);
    const VectorNetwork::Edge *left = FindEdge(network, leftEdge);
    const VectorNetwork::Edge *right = FindEdge(network, rightEdge);
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_FLOAT_EQ(left->endTangent.y, 4.0f);
    EXPECT_FLOAT_EQ(right->startTangent.y, -4.0f);
}

TEST(VectorNetworkEditTest, InsertVertexOnEdgeSplitsCubic) {
    VectorNetwork network =
        BezierPathToVectorNetwork(MakeSingleContour({{{0, 0}, {}, {0, 0}}, {{10, 0}, {0, 0}, {}}}, false));
    ASSERT_EQ(network.edges.size(), 1u);
    uint32_t midId = 0;
    network = InsertVertexOnEdge(network, network.edges[0].id, 0.5f, &midId);
    EXPECT_NE(midId, 0u);
    EXPECT_EQ(network.vertices.size(), 3u);
    EXPECT_EQ(network.edges.size(), 2u);
    const VectorNetwork::Vertex *mid = FindVertex(network, midId);
    ASSERT_NE(mid, nullptr);
    EXPECT_NEAR(mid->point.x, 5.0f, 1e-4f);
}

TEST(VectorNetworkEditTest, RemoveVertexDeletesIncidentEdges) {
    VectorNetwork network = ClosedTriangle();
    const uint32_t id = network.vertices[0].id;
    network = RemoveVertex(network, id);
    EXPECT_EQ(network.vertices.size(), 2u);
    EXPECT_EQ(network.edges.size(), 1u);
}

TEST(VectorNetworkEditTest, RecenterNetworkMovesAabbCenterToOrigin) {
    VectorNetwork network = ClosedTriangle();
    Vec2 center{};
    network = RecenterNetwork(network, center);
    EXPECT_NEAR(center.x, 5.0f, 1e-4f);
    EXPECT_NEAR(center.y, 5.0f, 1e-4f);
    Vec2 again{};
    network = RecenterNetwork(network, again);
    EXPECT_FLOAT_EQ(again.x, 0.0f);
    EXPECT_FLOAT_EQ(again.y, 0.0f);
}

TEST(VectorNetworkEditTest, RemoveEdgeKeepsVertices) {
    VectorNetwork network = ClosedTriangle();
    const uint32_t edgeId = network.edges[0].id;
    network = RemoveEdge(network, edgeId);
    EXPECT_EQ(network.edges.size(), 2u);
    EXPECT_EQ(network.vertices.size(), 3u);
}

TEST(VectorNetworkEditTest, ToggleSmoothNoOpWhenDegreeNotTwo) {
    // Hub with three spokes — shared vertex must not auto-smooth / rewrite topology.
    VectorNetwork network;
    uint32_t hub = 0;
    uint32_t a = 0;
    uint32_t b = 0;
    uint32_t c = 0;
    network = AddVertex(network, {0, 0}, &hub);
    network = AddVertex(network, {10, 0}, &a);
    network = AddVertex(network, {0, 10}, &b);
    network = AddVertex(network, {-10, 0}, &c);
    uint32_t e1 = 0;
    uint32_t e2 = 0;
    uint32_t e3 = 0;
    network = AddEdge(network, hub, a, &e1);
    network = AddEdge(network, hub, b, &e2);
    network = AddEdge(network, hub, c, &e3);
    ASSERT_EQ(VertexDegree(network, hub), 3);

    const VectorNetwork before = network;
    network = ToggleVertexSmooth(network, hub);
    EXPECT_EQ(network, before);
}

TEST(VectorNetworkEditTest, ToggleSmoothCornerToSmoothWhenDegreeTwo) {
    VectorNetwork network =
        BezierPathToVectorNetwork(MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{20, 0}, {}, {}}}, false));
    ASSERT_EQ(network.edges.size(), 2u);
    const uint32_t mid = network.vertices[1].id;
    ASSERT_EQ(VertexDegree(network, mid), 2);

    network = ToggleVertexSmooth(network, mid);
    const VectorNetwork::Edge *left = FindEdge(network, network.edges[0].id);
    const VectorNetwork::Edge *right = FindEdge(network, network.edges[1].id);
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_NE(left->endTangent.x, 0.0f);
    EXPECT_NE(right->startTangent.x, 0.0f);
    EXPECT_EQ(FindVertex(network, mid)->mirrorMode, VertexMirrorMode::Angle);

    network = ToggleVertexSmooth(network, mid);
    left = FindEdge(network, network.edges[0].id);
    right = FindEdge(network, network.edges[1].id);
    ASSERT_NE(left, nullptr);
    ASSERT_NE(right, nullptr);
    EXPECT_FLOAT_EQ(left->endTangent.x, 0.0f);
    EXPECT_FLOAT_EQ(left->endTangent.y, 0.0f);
    EXPECT_FLOAT_EQ(right->startTangent.x, 0.0f);
    EXPECT_FLOAT_EQ(right->startTangent.y, 0.0f);
    EXPECT_EQ(FindVertex(network, mid)->mirrorMode, VertexMirrorMode::None);
}

TEST(VectorNetworkEditTest, SetMirrorModeNoneClearsHandles) {
    VectorNetwork network = MakeDegreeTwoChain();
    network = SetVertexMirrorMode(network, 2, VertexMirrorMode::Angle);
    network = SetVertexMirrorMode(network, 2, VertexMirrorMode::None);
    EXPECT_EQ(FindVertex(network, 2)->mirrorMode, VertexMirrorMode::None);
    EXPECT_FLOAT_EQ(HandleAt(network, 1, 2).x, 0.0f);
    EXPECT_FLOAT_EQ(HandleAt(network, 1, 2).y, 0.0f);
    EXPECT_FLOAT_EQ(HandleAt(network, 2, 2).x, 0.0f);
    EXPECT_FLOAT_EQ(HandleAt(network, 2, 2).y, 0.0f);
}

TEST(VectorNetworkEditTest, SetMirrorModeAngleGeneratesIndependentLengths) {
    VectorNetwork network = MakeDegreeTwoChain();
    network = SetVertexMirrorMode(network, 2, VertexMirrorMode::Angle);
    EXPECT_EQ(FindVertex(network, 2)->mirrorMode, VertexMirrorMode::Angle);
    const Vec2 inHandle = HandleAt(network, 1, 2);
    const Vec2 outHandle = HandleAt(network, 2, 2);
    EXPECT_NEAR(HandleLength(inHandle), 10.0f / 3.0f, 1e-4f);
    EXPECT_NEAR(HandleLength(outHandle), 10.0f / 3.0f, 1e-4f);
    EXPECT_NEAR(inHandle.x * outHandle.y - inHandle.y * outHandle.x, 0.0f, 1e-4f);
    EXPECT_LT(inHandle.x * outHandle.x + inHandle.y * outHandle.y, 0.0f);
}

TEST(VectorNetworkEditTest, SetMirrorModeAngleLengthEqualizes) {
    VectorNetwork network;
    network.vertices = {{1, {0, 0}}, {2, {10, 0}}, {3, {16, 0}}};
    network.edges = {{1, 1, 2, {}, {}}, {2, 2, 3, {}, {}}};
    network = SetVertexMirrorMode(network, 2, VertexMirrorMode::AngleLength);
    EXPECT_EQ(FindVertex(network, 2)->mirrorMode, VertexMirrorMode::AngleLength);
    const float expected = ((10.0f / 3.0f) + (6.0f / 3.0f)) * 0.5f;
    EXPECT_NEAR(HandleLength(HandleAt(network, 1, 2)), expected, 1e-4f);
    EXPECT_NEAR(HandleLength(HandleAt(network, 2, 2)), expected, 1e-4f);
}

TEST(VectorNetworkEditTest, SetMirrorModeDegreeNotTwoWritesModeOnly) {
    VectorNetwork network;
    uint32_t hub = 0;
    uint32_t a = 0;
    uint32_t b = 0;
    uint32_t c = 0;
    network = AddVertex(network, {0, 0}, &hub);
    network = AddVertex(network, {10, 0}, &a);
    network = AddVertex(network, {0, 10}, &b);
    network = AddVertex(network, {-10, 0}, &c);
    network = AddEdge(network, hub, a, nullptr);
    network = AddEdge(network, hub, b, nullptr);
    network = AddEdge(network, hub, c, nullptr);
    ASSERT_EQ(VertexDegree(network, hub), 3);

    const VectorNetwork before = network;
    network = SetVertexMirrorMode(network, hub, VertexMirrorMode::AngleLength);
    EXPECT_EQ(FindVertex(network, hub)->mirrorMode, VertexMirrorMode::AngleLength);
    EXPECT_EQ(network.edges, before.edges);
}

TEST(VectorNetworkEditTest, MoveEdgeTangentAnglePreservesOppositeLength) {
    VectorNetwork network = MakeDegreeTwoChain();
    network = SetVertexMirrorMode(network, 2, VertexMirrorMode::Angle);
    const float preserved = HandleLength(HandleAt(network, 1, 2));
    network = MoveEdgeTangent(network, 2, true, {6, 0}, true);
    EXPECT_FLOAT_EQ(HandleAt(network, 2, 2).x, 6.0f);
    EXPECT_NEAR(HandleLength(HandleAt(network, 1, 2)), preserved, 1e-4f);
    EXPECT_LT(HandleAt(network, 1, 2).x, 0.0f);
}

TEST(VectorNetworkEditTest, MoveEdgeTangentAngleLengthNegates) {
    VectorNetwork network = MakeDegreeTwoChain();
    network = SetVertexMirrorMode(network, 2, VertexMirrorMode::AngleLength);
    network = MoveEdgeTangent(network, 2, true, {4, 0}, true);
    EXPECT_FLOAT_EQ(HandleAt(network, 2, 2).x, 4.0f);
    EXPECT_FLOAT_EQ(HandleAt(network, 1, 2).x, -4.0f);
}

TEST(VectorNetworkEditTest, MoveEdgeTangentMirrorFalseDoesNotTouchOther) {
    VectorNetwork network = MakeDegreeTwoChain();
    network = SetVertexMirrorMode(network, 2, VertexMirrorMode::AngleLength);
    const Vec2 beforeOther = HandleAt(network, 1, 2);
    network = MoveEdgeTangent(network, 2, true, {5, 0}, false);
    EXPECT_FLOAT_EQ(HandleAt(network, 2, 2).x, 5.0f);
    EXPECT_EQ(HandleAt(network, 1, 2), beforeOther);
}

TEST(VectorNetworkEditTest, MoveEdgeTangentNoneIgnoresMirrorFlagForOtherSide) {
    VectorNetwork network = MakeDegreeTwoChain();
    EXPECT_EQ(FindVertex(network, 2)->mirrorMode, VertexMirrorMode::None);
    network = MoveEdgeTangent(network, 2, true, {3, 0}, true);
    EXPECT_FLOAT_EQ(HandleAt(network, 2, 2).x, 3.0f);
    EXPECT_FLOAT_EQ(HandleAt(network, 1, 2).x, 0.0f);
}
