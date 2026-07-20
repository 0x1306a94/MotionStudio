#include <cmath>

#include <gtest/gtest.h>

#include "MotionStudio/animation/PathResample.h"

using motion::ApproxEqual;
using motion::BezierPath;
using motion::ResamplePath;
using motion::Vec2;

namespace {

// Open straight segment (0,0) -> (10,0).
BezierPath makeLine() {
    BezierPath path;
    path.vertices.push_back({{0, 0}, {}, {}});
    path.vertices.push_back({{10, 0}, {}, {}});
    return path;
}

// Closed unit square, corners in order.
BezierPath makeSquare() {
    BezierPath path;
    path.closed = true;
    path.vertices.push_back({{0, 0}, {}, {}});
    path.vertices.push_back({{1, 0}, {}, {}});
    path.vertices.push_back({{1, 1}, {}, {}});
    path.vertices.push_back({{0, 1}, {}, {}});
    return path;
}

bool OnSquarePerimeter(Vec2 point) {
    const bool onHorizontal = (ApproxEqual(point.y, 0.0f, 1e-3f) ||
                               ApproxEqual(point.y, 1.0f, 1e-3f)) &&
        point.x >= -1e-3f && point.x <= 1.0f + 1e-3f;
    const bool onVertical = (ApproxEqual(point.x, 0.0f, 1e-3f) ||
                             ApproxEqual(point.x, 1.0f, 1e-3f)) &&
        point.y >= -1e-3f && point.y <= 1.0f + 1e-3f;
    return onHorizontal || onVertical;
}

}  // namespace

TEST(PathResampleTest, ReturnsUnchangedWhenCountMatches) {
    BezierPath line = makeLine();
    BezierPath result = ResamplePath(line, 2);
    EXPECT_EQ(result, line);
}

TEST(PathResampleTest, ReturnsUnchangedForDegenerateInput) {
    BezierPath empty;
    EXPECT_EQ(ResamplePath(empty, 5), empty);

    BezierPath single;
    single.vertices.push_back({{1, 1}, {}, {}});
    EXPECT_EQ(ResamplePath(single, 5), single);

    BezierPath line = makeLine();
    EXPECT_EQ(ResamplePath(line, 1), line);
}

TEST(PathResampleTest, OpenLineGetsEvenlySpacedVertices) {
    BezierPath result = ResamplePath(makeLine(), 5);
    ASSERT_EQ(result.vertices.size(), 5u);
    EXPECT_FALSE(result.closed);
    for (size_t i = 0; i < 5; ++i) {
        EXPECT_TRUE(ApproxEqual(result.vertices[i].point, Vec2{float(i) * 2.5f, 0}))
            << "i=" << i;
    }
}

TEST(PathResampleTest, ClosedSquareStaysOnPerimeter) {
    BezierPath result = ResamplePath(makeSquare(), 8);
    ASSERT_EQ(result.vertices.size(), 8u);
    EXPECT_TRUE(result.closed);
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_TRUE(OnSquarePerimeter(result.vertices[i].point)) << "i=" << i;
    }
    // Perimeter preserved: consecutive distances sum to 4.
    float perimeter = 0;
    for (size_t i = 0; i < 8; ++i) {
        const Vec2 delta =
            result.vertices[(i + 1) % 8].point - result.vertices[i].point;
        perimeter += std::sqrt(delta.x * delta.x + delta.y * delta.y);
    }
    EXPECT_NEAR(perimeter, 4.0f, 1e-2f);
}

TEST(PathResampleTest, ResampleDownsampling) {
    BezierPath square = makeSquare();
    BezierPath denser = ResamplePath(square, 16);
    BezierPath back = ResamplePath(denser, 4);
    ASSERT_EQ(back.vertices.size(), 4u);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(OnSquarePerimeter(back.vertices[i].point)) << "i=" << i;
    }
}
