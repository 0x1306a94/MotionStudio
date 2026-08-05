#include <gtest/gtest.h>

#include "MotionStudio/animation/Interpolator.h"
#include "MotionStudio/common/VectorNetwork.h"

using motion::Interpolator;
using motion::VectorNetwork;

namespace {

VectorNetwork MakeSharedPointNetwork(float sharedY) {
    VectorNetwork network;
    network.vertices = {
        {1, {0, sharedY}},
        {2, {10, 0}},
        {3, {-10, 0}},
    };
    network.edges = {
        {1, 1, 2, {}, {}},
        {2, 1, 3, {}, {}},
    };
    return network;
}

}  // namespace

TEST(VectorNetworkMorphTest, SameTopologyLerpsSharedPointOnce) {
    const VectorNetwork from = MakeSharedPointNetwork(0.0f);
    const VectorNetwork to = MakeSharedPointNetwork(10.0f);
    const VectorNetwork mid = Interpolator<VectorNetwork>::Lerp(from, to, 0.5f);
    ASSERT_EQ(mid.vertices.size(), 3u);
    EXPECT_EQ(mid.vertices[0].id, 1u);
    EXPECT_FLOAT_EQ(mid.vertices[0].point.y, 5.0f);
    EXPECT_EQ(mid.edges.size(), 2u);
    EXPECT_EQ(mid.edges[0].start, 1u);
    EXPECT_EQ(mid.edges[0].end, 2u);
}

TEST(VectorNetworkMorphTest, DifferentTopologyHoldsFrom) {
    VectorNetwork from = MakeSharedPointNetwork(0.0f);
    VectorNetwork to = from;
    to.edges.push_back({3, 2, 3, {}, {}});
    EXPECT_EQ(Interpolator<VectorNetwork>::Lerp(from, to, 0.5f), from);
}

TEST(VectorNetworkMorphTest, SameTopologyLerpsEdgeTangents) {
    VectorNetwork from = MakeSharedPointNetwork(0.0f);
    from.edges[0].startTangent = {0, 0};
    from.edges[0].endTangent = {0, 0};
    VectorNetwork to = from;
    to.edges[0].startTangent = {4, 0};
    to.edges[0].endTangent = {-4, 0};
    const VectorNetwork mid = Interpolator<VectorNetwork>::Lerp(from, to, 0.5f);
    ASSERT_EQ(mid.edges.size(), 1u + 1u);
    EXPECT_FLOAT_EQ(mid.edges[0].startTangent.x, 2.0f);
    EXPECT_FLOAT_EQ(mid.edges[0].endTangent.x, -2.0f);
}

TEST(VectorNetworkMorphTest, MirrorModeHoldsFromLeftKeyframe) {
    VectorNetwork from = MakeSharedPointNetwork(0.0f);
    from.vertices[0].mirrorMode = motion::VertexMirrorMode::Angle;
    VectorNetwork to = from;
    to.vertices[0].mirrorMode = motion::VertexMirrorMode::AngleLength;
    to.vertices[0].point = {0, 10};
    const VectorNetwork mid = Interpolator<VectorNetwork>::Lerp(from, to, 0.5f);
    EXPECT_EQ(mid.vertices[0].mirrorMode, motion::VertexMirrorMode::Angle);
    EXPECT_FLOAT_EQ(mid.vertices[0].point.y, 5.0f);
}
