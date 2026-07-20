#include <gtest/gtest.h>

#include "MotionStudio/common/BezierPath.h"

using motion::BezierPath;
using motion::Vec2;

TEST(BezierPathTest, DefaultsToEmptyAndOpen) {
    BezierPath path;
    EXPECT_TRUE(path.vertices.empty());
    EXPECT_FALSE(path.closed);
}

TEST(BezierPathTest, EqualityComparesVerticesAndClosedFlag) {
    BezierPath pathA;
    pathA.vertices.push_back({{0, 0}, {-1, 0}, {1, 0}});
    pathA.vertices.push_back({{10, 0}, {-1, 0}, {1, 0}});
    pathA.closed = true;

    BezierPath pathB = pathA;
    EXPECT_EQ(pathA, pathB);

    pathB.vertices[0].outTangent = {2, 0};
    EXPECT_NE(pathA, pathB);

    pathB = pathA;
    pathB.closed = false;
    EXPECT_NE(pathA, pathB);
}
