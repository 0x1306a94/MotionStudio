#include <cmath>

#include <gtest/gtest.h>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/common/VectorNetwork.h"
#include "MotionStudio/common/VectorNetworkCompile.h"
#include "MotionStudio/common/VectorNetworkConvert.h"

using motion::BezierPath;
using motion::BezierPathToVectorNetwork;
using motion::CompileFillFaces;
using motion::CompileStrokeEdges;
using motion::MakeSingleContour;
using motion::VectorNetwork;

namespace {

VectorNetwork MakeTriangleFan() {
    // P0 inside outer triangle P1-P2-P3; spokes + rim.
    VectorNetwork network;
    network.vertices = {
        {1, {0, 2}},
        {2, {6, 0}},
        {3, {0, 8}},
        {4, {-6, 0}},
    };
    // Edges: P0-P1, P0-P2, P0-P3, P1-P2, P2-P3, P3-P1
    network.edges = {
        {1, 1, 2, {}, {}},
        {2, 1, 3, {}, {}},
        {3, 1, 4, {}, {}},
        {4, 2, 3, {}, {}},
        {5, 3, 4, {}, {}},
        {6, 4, 2, {}, {}},
    };
    return network;
}

BezierPath MakeOpenSegment() {
    return MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}}, false);
}

}  // namespace

TEST(VectorNetworkCompileTest, TriangleFanYieldsThreeFillFaces) {
    const VectorNetwork network = MakeTriangleFan();
    const BezierPath fill = CompileFillFaces(network);
    EXPECT_EQ(fill.contours.size(), 3u);
    for (const BezierPath::Contour &contour : fill.contours) {
        EXPECT_TRUE(contour.closed);
        EXPECT_GE(contour.vertices.size(), 3u);
    }
    const BezierPath stroke = CompileStrokeEdges(network);
    EXPECT_EQ(stroke.contours.size(), 6u);
}

TEST(VectorNetworkCompileTest, OpenChainHasNoFill) {
    const VectorNetwork network = BezierPathToVectorNetwork(MakeOpenSegment());
    EXPECT_TRUE(CompileFillFaces(network).contours.empty());
    EXPECT_EQ(CompileStrokeEdges(network).contours.size(), 1u);
}

TEST(VectorNetworkCompileTest, ClosedTriangleYieldsOneFillFace) {
    const BezierPath path =
        MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{0, 10}, {}, {}}}, true);
    const VectorNetwork network = BezierPathToVectorNetwork(path);
    const BezierPath fill = CompileFillFaces(network);
    ASSERT_EQ(fill.contours.size(), 1u);
    EXPECT_TRUE(fill.contours[0].closed);
    EXPECT_GE(fill.contours[0].vertices.size(), 3u);
    // Degree-2 ring chains into one closed stroke so joins apply at corners.
    const BezierPath stroke = CompileStrokeEdges(network);
    ASSERT_EQ(stroke.contours.size(), 1u);
    EXPECT_TRUE(stroke.contours[0].closed);
    EXPECT_EQ(stroke.contours[0].vertices.size(), 3u);
}

TEST(VectorNetworkCompileTest, ClosedTrapezoidStrokeChainsForJoins) {
    // Path 8 topology: closed 4-gon. One open contour per edge leaves butt gaps
    // at corners; stroke must be a single closed contour.
    VectorNetwork network;
    network.vertices = {
        {1, {-171.8f, -173.4f}},
        {2, {174.1f, -174.5f}},
        {3, {215.8f, 172.2f}},
        {4, {-215.8f, 174.5f}},
    };
    network.edges = {
        {1, 1, 2, {}, {}},
        {2, 2, 3, {}, {}},
        {3, 3, 4, {}, {}},
        {4, 4, 1, {}, {}},
    };
    const BezierPath stroke = CompileStrokeEdges(network);
    ASSERT_EQ(stroke.contours.size(), 1u);
    EXPECT_TRUE(stroke.contours[0].closed);
    EXPECT_EQ(stroke.contours[0].vertices.size(), 4u);
}

TEST(VectorNetworkCompileTest, OpenPolylineChainsThroughDegreeTwo) {
    const BezierPath path =
        MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}, {{20, 5}, {}, {}}}, false);
    const VectorNetwork network = BezierPathToVectorNetwork(path);
    const BezierPath stroke = CompileStrokeEdges(network);
    ASSERT_EQ(stroke.contours.size(), 1u);
    EXPECT_FALSE(stroke.contours[0].closed);
    EXPECT_EQ(stroke.contours[0].vertices.size(), 3u);
}

TEST(VectorNetworkCompileTest, StrokePreservesEdgeTangents) {
    BezierPath path = MakeSingleContour({{{0, 0}, {}, {1, 0}}, {{10, 0}, {-2, 0}, {}}}, false);
    const VectorNetwork network = BezierPathToVectorNetwork(path);
    const BezierPath stroke = CompileStrokeEdges(network);
    ASSERT_EQ(stroke.contours.size(), 1u);
    ASSERT_EQ(stroke.contours[0].vertices.size(), 2u);
    EXPECT_FALSE(stroke.contours[0].closed);
    EXPECT_FLOAT_EQ(stroke.contours[0].vertices[0].outTangent.x, 1.0f);
    EXPECT_FLOAT_EQ(stroke.contours[0].vertices[1].inTangent.x, -2.0f);
}

// Path 5 topology (disconnected left digon-ish triangles + right pentagon) —
// reproduce fill-face count for diagnosis.
TEST(VectorNetworkCompileTest, DisconnectedComponentsFillBoth) {
    VectorNetwork network;
    network.vertices = {
        {1, {-187.f, -136.6f}},
        {2, {26.9f, -110.2f}},
        {3, {-55.2f, 40.9f}},
        {4, {-179.f, 76.2f}},
        {5, {37.8f, 16.5f}},
        {6, {-97.4f, 98.6f}},
        {7, {36.2f, 136.6f}},
        {8, {187.f, 42.2f}},
        {9, {61.5f, -73.6f}},
    };
    // Left cluster includes reverse duplicates (1↔4, 2↔7) as in the saved doc.
    network.edges = {
        {1, 1, 2, {}, {}},
        {2, 2, 3, {}, {}},
        {3, 3, 1, {}, {}},
        {4, 2, 1, {}, {}},
        {5, 1, 4, {}, {}},
        {6, 4, 3, {}, {}},
        {7, 3, 2, {}, {}},
        {8, 5, 6, {}, {}},
        {9, 6, 7, {}, {}},
        {10, 7, 8, {}, {}},
        {11, 8, 9, {}, {}},
        {12, 9, 5, {}, {}},
    };
    const BezierPath withDups = CompileFillFaces(network);
    VectorNetwork clean = network;
    clean.edges = {
        {1, 1, 2, {}, {}},
        {2, 2, 3, {}, {}},
        {3, 3, 1, {}, {}},
        {5, 1, 4, {}, {}},
        {6, 4, 3, {}, {}},
        {8, 5, 6, {}, {}},
        {9, 6, 7, {}, {}},
        {10, 7, 8, {}, {}},
        {11, 8, 9, {}, {}},
        {12, 9, 5, {}, {}},
    };
    const BezierPath cleaned = CompileFillFaces(clean);
    // Left two triangles + right pentagon, regardless of reverse-duplicate edges.
    // Contours are densely sampled polylines, so only count faces (not vertex arity).
    EXPECT_EQ(withDups.contours.size(), 3u);
    EXPECT_EQ(cleaned.contours.size(), 3u);
    for (const BezierPath::Contour &contour : withDups.contours) {
        EXPECT_TRUE(contour.closed);
        EXPECT_GE(contour.vertices.size(), 3u);
    }
}

// Path 5 keyframe 0 after inserting vertex 10 on the right component while
// leaving the original 9→5 edge (ear + shared edge).
TEST(VectorNetworkCompileTest, RightEarWithSharedEdgeFills) {
    VectorNetwork network;
    network.vertices = {
        {1, {-187.f, -136.6f}},
        {2, {26.9f, -110.2f}},
        {3, {-46.f, -17.6f}},
        {4, {-179.f, 76.2f}},
        {5, {37.8f, 16.5f}},
        {6, {-97.4f, 98.6f}},
        {7, {36.2f, 136.6f}},
        {8, {187.f, 42.2f}},
        {9, {61.5f, -73.6f}},
        {10, {-13.7f, -22.9f}},
    };
    network.edges = {
        {1, 1, 2, {}, {}},
        {2, 2, 3, {}, {}},
        {3, 3, 1, {}, {}},
        {4, 2, 1, {}, {}},
        {5, 1, 4, {}, {-42.3f, -57.f}},
        {6, 4, 3, {25.6f, 34.5f}, {}},
        {7, 3, 2, {}, {}},
        {8, 5, 6, {-35.8f, 38.7f}, {0.7f, -52.7f}},
        {9, 6, 7, {-0.6f, 46.3f}, {-151.6f, -27.8f}},
        {10, 7, 8, {151.6f, 27.8f}, {}},
        {11, 8, 9, {}, {75.7f, 12.7f}},
        {12, 9, 5, {-75.7f, -12.7f}, {21.1f, -22.8f}},
        {13, 9, 10, {}, {}},
        {14, 10, 5, {}, {}},
    };
    const BezierPath fill = CompileFillFaces(network);
    // Crossing cubic splits the ear into multiple visual pockets; all fill.
    EXPECT_GE(fill.contours.size(), 4u);
    for (const BezierPath::Contour &contour : fill.contours) {
        EXPECT_TRUE(contour.closed);
        EXPECT_GE(contour.vertices.size(), 3u);
    }
}

TEST(VectorNetworkCompileTest, BowedTriangleWithoutCrossingStillOneFace) {
    VectorNetwork network;
    network.vertices = {{1, {0, 0}}, {2, {10, 0}}, {3, {5, 8}}};
    // Mild bow on 1→2 that stays below the triangle — no edge crossings.
    network.edges = {
        {1, 1, 2, {2, -1}, {-2, -1}},
        {2, 2, 3, {}, {}},
        {3, 3, 1, {}, {}},
    };
    const BezierPath fill = CompileFillFaces(network);
    ASSERT_EQ(fill.contours.size(), 1u);
    EXPECT_TRUE(fill.contours[0].closed);
    EXPECT_GE(fill.contours[0].vertices.size(), 3u);
}

namespace {

float ContourSignedArea(const BezierPath::Contour &contour) {
    if (contour.vertices.size() < 3) {
        return 0.0f;
    }
    float area = 0.0f;
    for (size_t index = 0; index < contour.vertices.size(); ++index) {
        const motion::Vec2 &a = contour.vertices[index].point;
        const motion::Vec2 &b = contour.vertices[(index + 1) % contour.vertices.size()].point;
        area += a.x * b.y - b.x * a.y;
    }
    return area * 0.5f;
}

VectorNetwork MakeNestedRingSquare() {
    // Outer CW square + disconnected inner CW square (letter-O style hole).
    // NonZero fill requires the hole contour to oppose the outer after compile.
    VectorNetwork network;
    network.vertices = {
        {1, {0, 0}},
        {2, {10, 0}},
        {3, {10, 10}},
        {4, {0, 10}},
        {5, {3, 3}},
        {6, {7, 3}},
        {7, {7, 7}},
        {8, {3, 7}},
    };
    network.edges = {
        {1, 1, 2, {}, {}},
        {2, 2, 3, {}, {}},
        {3, 3, 4, {}, {}},
        {4, 4, 1, {}, {}},
        {5, 5, 6, {}, {}},
        {6, 6, 7, {}, {}},
        {7, 7, 8, {}, {}},
        {8, 8, 5, {}, {}},
    };
    return network;
}

}  // namespace

TEST(VectorNetworkCompileTest, DisconnectedHoleContourOpposesOuterForNonZero) {
    const BezierPath fill = CompileFillFaces(MakeNestedRingSquare());
    ASSERT_EQ(fill.contours.size(), 2u);
    const float outerArea = ContourSignedArea(fill.contours[0]);
    const float holeArea = ContourSignedArea(fill.contours[1]);
    // Assign by |area|: larger is outer.
    const float large = std::fabs(outerArea) >= std::fabs(holeArea) ? outerArea : holeArea;
    const float small = std::fabs(outerArea) >= std::fabs(holeArea) ? holeArea : outerArea;
    EXPECT_GT(std::fabs(large), std::fabs(small));
    EXPECT_LT(large * small, 0.0f);
}

TEST(VectorNetworkCompileTest, DisconnectedHoleStillOpposesOuterAfterUniformScale) {
    VectorNetwork network = MakeNestedRingSquare();
    for (VectorNetwork::Vertex &vertex : network.vertices) {
        vertex.point.x *= 0.05f;
        vertex.point.y *= 0.05f;
    }
    const BezierPath fill = CompileFillFaces(network);
    ASSERT_EQ(fill.contours.size(), 2u);
    float areas[2] = {ContourSignedArea(fill.contours[0]), ContourSignedArea(fill.contours[1])};
    if (std::fabs(areas[0]) < std::fabs(areas[1])) {
        const float tmp = areas[0];
        areas[0] = areas[1];
        areas[1] = tmp;
    }
    EXPECT_LT(areas[0] * areas[1], 0.0f);
}
