#include <gtest/gtest.h>

#include "MotionStudio/common/VectorNetwork.h"
#include "MotionStudio/common/VectorNetworkConvert.h"

using motion::BezierPath;
using motion::BezierPathToVectorNetwork;
using motion::MakeSingleContour;
using motion::VectorNetwork;
using motion::VertexDegree;

TEST(VectorNetworkTest, ClosedTriangleConvertsToThreeEdges) {
    BezierPath path = MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{0, 10}, {}, {}}}, true);
    const VectorNetwork network = BezierPathToVectorNetwork(path);
    EXPECT_EQ(network.vertices.size(), 3u);
    EXPECT_EQ(network.edges.size(), 3u);
    EXPECT_EQ(VertexDegree(network, network.vertices[0].id), 2);
}

TEST(VectorNetworkTest, OpenPolylineHasNMinusOneEdges) {
    BezierPath path = MakeSingleContour({{{0, 0}, {}, {}}, {{1, 0}, {}, {}}, {{2, 0}, {}, {}}}, false);
    const VectorNetwork network = BezierPathToVectorNetwork(path);
    EXPECT_EQ(network.edges.size(), 2u);
}

TEST(VectorNetworkTest, TangentsMapOntoEdges) {
    BezierPath path = MakeSingleContour({{{0, 0}, {}, {1, 0}}, {{10, 0}, {-1, 0}, {}}}, false);
    const VectorNetwork network = BezierPathToVectorNetwork(path);
    ASSERT_EQ(network.edges.size(), 1u);
    EXPECT_FLOAT_EQ(network.edges[0].startTangent.x, 1.0f);
    EXPECT_FLOAT_EQ(network.edges[0].endTangent.x, -1.0f);
}
