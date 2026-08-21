#include <gtest/gtest.h>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/VectorNetwork.h"

using motion::BezierPath;
using motion::VectorNetwork;

TEST(GeometryRevisionTest, DefaultIsZeroAndStampIsNonZeroAndIncreases) {
    VectorNetwork network;
    EXPECT_EQ(motion::detail::GeometryRevision(network), 0u);
    motion::detail::StampGeometryRevision(network);
    const uint64_t first = motion::detail::GeometryRevision(network);
    EXPECT_NE(first, 0u);
    motion::detail::StampGeometryRevision(network);
    EXPECT_GT(motion::detail::GeometryRevision(network), first);

    BezierPath path;
    EXPECT_EQ(motion::detail::GeometryRevision(path), 0u);
    motion::detail::StampGeometryRevision(path);
    const uint64_t pathFirst = motion::detail::GeometryRevision(path);
    EXPECT_NE(pathFirst, 0u);
    motion::detail::StampGeometryRevision(path);
    EXPECT_GT(motion::detail::GeometryRevision(path), pathFirst);
}

TEST(GeometryRevisionTest, CopyKeepsRevisionUntilRestamped) {
    VectorNetwork network;
    network.vertices.push_back({1, {0, 0}});
    motion::detail::StampGeometryRevision(network);
    const uint64_t stamped = motion::detail::GeometryRevision(network);
    const VectorNetwork copied = network;
    EXPECT_EQ(motion::detail::GeometryRevision(copied), stamped);
    network.vertices[0].point = {1, 0};
    motion::detail::StampGeometryRevision(network);
    EXPECT_NE(motion::detail::GeometryRevision(network), stamped);
    EXPECT_EQ(motion::detail::GeometryRevision(copied), stamped);
}

TEST(GeometryRevisionTest, EqualityIgnoresRevision) {
    VectorNetwork left;
    left.vertices.push_back({1, {0, 0}});
    VectorNetwork right = left;
    motion::detail::StampGeometryRevision(left);
    motion::detail::StampGeometryRevision(right);
    EXPECT_NE(motion::detail::GeometryRevision(left), motion::detail::GeometryRevision(right));
    EXPECT_EQ(left, right);

    BezierPath pathLeft;
    pathLeft.contours.push_back({{{0, 0}, {}, {}}, false});
    BezierPath pathRight = pathLeft;
    motion::detail::StampGeometryRevision(pathLeft);
    motion::detail::StampGeometryRevision(pathRight);
    EXPECT_EQ(pathLeft, pathRight);
}
