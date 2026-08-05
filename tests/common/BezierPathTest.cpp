#include <gtest/gtest.h>

#include "MotionStudio/common/BezierPath.h"

using motion::BezierPath;
using motion::IsSingleContour;
using motion::MakeSingleContour;
using motion::PrimaryContour;
using motion::Vec2;

TEST(BezierPathTest, DefaultsToEmpty) {
    BezierPath path;
    EXPECT_TRUE(path.contours.empty());
}

TEST(BezierPathTest, MultiContourEquality) {
    BezierPath a = MakeSingleContour({{{0, 0}, {}, {}}, {{1, 0}, {}, {}}}, true);
    BezierPath b = a;
    EXPECT_EQ(a, b);
    b.contours.push_back(a.contours.front());
    EXPECT_NE(a, b);
}

TEST(BezierPathTest, EqualityComparesVerticesAndClosedFlag) {
    BezierPath pathA = MakeSingleContour({{{0, 0}, {-1, 0}, {1, 0}}, {{10, 0}, {-1, 0}, {1, 0}}}, true);

    BezierPath pathB = pathA;
    EXPECT_EQ(pathA, pathB);

    pathB.contours[0].vertices[0].outTangent = {2, 0};
    EXPECT_NE(pathA, pathB);

    pathB = pathA;
    pathB.contours[0].closed = false;
    EXPECT_NE(pathA, pathB);
}

TEST(BezierPathTest, IsZeroForEmptyCollapsedAndHairline) {
    BezierPath empty;
    EXPECT_TRUE(empty.isZero());

    BezierPath single = MakeSingleContour({{{4, 5}, {}, {}}}, false);
    EXPECT_TRUE(single.isZero());

    BezierPath collapsed = MakeSingleContour({{{40, 50}, {}, {}}, {{40, 50}, {}, {}}}, false);
    EXPECT_TRUE(collapsed.isZero());

    BezierPath hairline = MakeSingleContour({{{0, 10}, {}, {}}, {{20, 10}, {}, {}}}, false);
    EXPECT_FALSE(hairline.isZero());

    BezierPath curvedFromPoint =
        MakeSingleContour({{{0, 0}, {}, {10, 0}}, {{0, 0}, {-10, 0}, {}}}, false);
    EXPECT_FALSE(curvedFromPoint.isZero());
}

TEST(BezierPathTest, MakeSingleContourAndHelpers) {
    BezierPath path = MakeSingleContour({{{0, 0}, {}, {}}, {{1, 0}, {}, {}}}, true);
    EXPECT_TRUE(IsSingleContour(path));
    ASSERT_NE(PrimaryContour(path), nullptr);
    EXPECT_EQ(PrimaryContour(path)->vertices.size(), 2u);
    EXPECT_TRUE(PrimaryContour(path)->closed);

    BezierPath multi = path;
    multi.contours.push_back(path.contours.front());
    EXPECT_FALSE(IsSingleContour(multi));

    BezierPath empty;
    EXPECT_FALSE(IsSingleContour(empty));
    EXPECT_EQ(PrimaryContour(empty), nullptr);
}
