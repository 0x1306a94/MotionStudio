#include <gtest/gtest.h>

#include "MotionStudio/animation/Interpolator.h"

using motion::BezierPath;
using motion::Color;
using motion::CubicBezierPoint;
using motion::Interpolator;
using motion::MakeSingleContour;
using motion::Vec2;

TEST(InterpolatorTest, FloatLerp) {
    EXPECT_FLOAT_EQ(Interpolator<float>::Lerp(0, 10, 0.25f), 2.5f);
}

TEST(InterpolatorTest, Vec2Lerp) {
    Vec2 result = Interpolator<Vec2>::Lerp({0, 0}, {10, 20}, 0.5f);
    EXPECT_EQ(result, (Vec2{5, 10}));
}

TEST(InterpolatorTest, ColorLerp) {
    Color result = Interpolator<Color>::Lerp({0, 0, 0, 0}, {1, 1, 1, 1}, 0.5f);
    EXPECT_EQ(result, (Color{0.5f, 0.5f, 0.5f, 0.5f}));
}

namespace {
BezierPath MakeTwoVertexPath(float x) {
    BezierPath path = MakeSingleContour({{{x, 0}, {-1, 0}, {1, 0}}, {{x + 10, 0}, {-1, 0}, {1, 0}}}, false);
    return path;
}
}  // namespace

TEST(InterpolatorTest, BezierPathLerpInterpolatesVertices) {
    BezierPath result =
        Interpolator<BezierPath>::Lerp(MakeTwoVertexPath(0), MakeTwoVertexPath(10), 0.5f);
    ASSERT_EQ(result.contours[0].vertices.size(), 2u);
    EXPECT_EQ(result.contours[0].vertices[0].point, (Vec2{5, 0}));
    EXPECT_EQ(result.contours[0].vertices[1].point, (Vec2{15, 0}));
}

TEST(InterpolatorTest, BezierPathLerpAutoMatchesVertexCounts) {
    // from: 2 vertices over [0,10]; to: 3 vertices over [0,20].
    BezierPath from = MakeTwoVertexPath(0);
    BezierPath to = MakeTwoVertexPath(0);
    to.contours[0].vertices.push_back({{20, 0}, {}, {}});

    // from gets resampled to 3 vertices ((0,0),(5,0),(10,0)) before blending.
    BezierPath result = Interpolator<BezierPath>::Lerp(from, to, 0.5f);
    ASSERT_EQ(result.contours[0].vertices.size(), 3u);
    EXPECT_TRUE(motion::ApproxEqual(result.contours[0].vertices[0].point, Vec2{0, 0}));
    EXPECT_TRUE(motion::ApproxEqual(result.contours[0].vertices[1].point, Vec2{7.5f, 0}));
    EXPECT_TRUE(motion::ApproxEqual(result.contours[0].vertices[2].point, Vec2{15, 0}));
}

TEST(InterpolatorTest, BezierPathLerpAssertsOnClosedFlagMismatch) {
    BezierPath open = MakeTwoVertexPath(0);
    BezierPath closed = MakeTwoVertexPath(0);
    closed.contours[0].closed = true;
    // Open/closed conversion is a data-contract violation: assert in debug.
    EXPECT_DEATH({ Interpolator<BezierPath>::Lerp(open, closed, 0.5f); },
                 "closed flags");
}

TEST(CubicBezierPointTest, MidpointOfStraightLine) {
    Vec2 mid = CubicBezierPoint({0, 0}, {1, 0}, {2, 0}, {3, 0}, 0.5f);
    EXPECT_TRUE(motion::ApproxEqual(mid, Vec2{1.5f, 0}));
}
