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

TEST(BezierPathTest, IsZeroForEmptyCollapsedAndHairline) {
    BezierPath empty;
    EXPECT_TRUE(empty.isZero());

    BezierPath single;
    single.vertices.push_back({{4, 5}, {}, {}});
    EXPECT_TRUE(single.isZero());

    BezierPath collapsed;
    collapsed.vertices.push_back({{40, 50}, {}, {}});
    collapsed.vertices.push_back({{40, 50}, {}, {}});
    EXPECT_TRUE(collapsed.isZero());

    BezierPath hairline;
    hairline.vertices.push_back({{0, 10}, {}, {}});
    hairline.vertices.push_back({{20, 10}, {}, {}});
    EXPECT_FALSE(hairline.isZero());

    BezierPath curvedFromPoint;
    curvedFromPoint.vertices.push_back({{0, 0}, {}, {10, 0}});
    curvedFromPoint.vertices.push_back({{0, 0}, {-10, 0}, {}});
    EXPECT_FALSE(curvedFromPoint.isZero());
}
