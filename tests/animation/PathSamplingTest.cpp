#include <gtest/gtest.h>

#include "MotionStudio/animation/PathSampling.h"
#include "MotionStudio/common/Vec2.h"

using motion::ApproxEqual;
using motion::BezierPath;
using motion::PathArcLength;
using motion::PathSample;
using motion::PointAndTangentAtArcLength;
using motion::Vec2;

namespace {

BezierPath MakeHorizontalSegment() {
    BezierPath path;
    path.closed = false;
    path.vertices.push_back({{0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}});
    path.vertices.push_back({{100.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}});
    return path;
}

}  // namespace

TEST(PathSamplingTest, HorizontalSegmentArcLength) {
    const BezierPath path = MakeHorizontalSegment();
    EXPECT_NEAR(PathArcLength(path), 100.0f, 0.1f);
}

TEST(PathSamplingTest, HorizontalSegmentSamplesEndpointsAndMidpoint) {
    const BezierPath path = MakeHorizontalSegment();

    const PathSample start = PointAndTangentAtArcLength(path, 0.0f);
    EXPECT_TRUE(ApproxEqual(start.point, {0.0f, 0.0f}, 0.1f));
    EXPECT_GT(start.tangent.x, 0.0f);
    EXPECT_NEAR(start.tangent.y, 0.0f, 0.1f);

    const PathSample mid = PointAndTangentAtArcLength(path, 50.0f);
    EXPECT_TRUE(ApproxEqual(mid.point, {50.0f, 0.0f}, 0.5f));
    EXPECT_GT(mid.tangent.x, 0.0f);

    const PathSample end = PointAndTangentAtArcLength(path, 100.0f);
    EXPECT_TRUE(ApproxEqual(end.point, {100.0f, 0.0f}, 0.1f));
    EXPECT_GT(end.tangent.x, 0.0f);
}

TEST(PathSamplingTest, ClampsBeyondTotalLength) {
    const BezierPath path = MakeHorizontalSegment();
    const PathSample pastEnd = PointAndTangentAtArcLength(path, 500.0f);
    EXPECT_TRUE(ApproxEqual(pastEnd.point, {100.0f, 0.0f}, 0.1f));
}

TEST(PathSamplingTest, EmptyPathReturnsOrigin) {
    BezierPath path;
    EXPECT_NEAR(PathArcLength(path), 0.0f, 1e-5f);
    const PathSample sample = PointAndTangentAtArcLength(path, 10.0f);
    EXPECT_TRUE(ApproxEqual(sample.point, {0.0f, 0.0f}));
    EXPECT_TRUE(ApproxEqual(sample.tangent, {1.0f, 0.0f}));
}
