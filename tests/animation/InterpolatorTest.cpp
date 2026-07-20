#include <gtest/gtest.h>

#include "MotionStudio/animation/Interpolator.h"

using motion::BezierPath;
using motion::Color;
using motion::CubicBezierPoint;
using motion::Interpolator;
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
    BezierPath path;
    path.vertices.push_back({{x, 0}, {-1, 0}, {1, 0}});
    path.vertices.push_back({{x + 10, 0}, {-1, 0}, {1, 0}});
    return path;
}
}  // namespace

TEST(InterpolatorTest, BezierPathLerpInterpolatesVertices) {
    BezierPath result =
        Interpolator<BezierPath>::Lerp(MakeTwoVertexPath(0), MakeTwoVertexPath(10), 0.5f);
    ASSERT_EQ(result.vertices.size(), 2u);
    EXPECT_EQ(result.vertices[0].point, (Vec2{5, 0}));
    EXPECT_EQ(result.vertices[1].point, (Vec2{15, 0}));
}

TEST(InterpolatorTest, BezierPathLerpAssertsOnVertexCountMismatch) {
    BezierPath two = MakeTwoVertexPath(0);
    BezierPath three = MakeTwoVertexPath(0);
    three.vertices.push_back({{30, 0}, {}, {}});
    // 顶点数不一致属数据约定违例：debug 下 assert 快速失败。
    EXPECT_DEATH({ Interpolator<BezierPath>::Lerp(two, three, 0.5f); }, "顶点数");
}

TEST(CubicBezierPointTest, MidpointOfStraightLine) {
    Vec2 mid = CubicBezierPoint({0, 0}, {1, 0}, {2, 0}, {3, 0}, 0.5f);
    EXPECT_TRUE(motion::ApproxEqual(mid, Vec2{1.5f, 0}));
}
