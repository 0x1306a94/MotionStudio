#include <gtest/gtest.h>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/render/ShapeGeometry.h"

using motion::BezierPath;
using motion::MakeEllipseGeometry;
using motion::MakePathGeometry;
using motion::MakeRectGeometry;
using motion::MakeSingleContour;
using motion::Vec2;

TEST(ShapeGeometryTest, IsZeroForPathRectEllipse) {
    BezierPath collapsed = MakeSingleContour({{{1, 2}, {}, {}}, {{1, 2}, {}, {}}}, false);
    EXPECT_TRUE(MakePathGeometry(collapsed).isZero());

    BezierPath hairline = MakeSingleContour({{{0, 0}, {}, {}}, {{10, 0}, {}, {}}}, false);
    EXPECT_FALSE(MakePathGeometry(hairline).isZero());

    EXPECT_TRUE(MakeRectGeometry({0, 0}, {0, 0}).isZero());
    EXPECT_FALSE(MakeRectGeometry({0, 0}, {10, 0}).isZero());
    EXPECT_FALSE(MakeRectGeometry({0, 0}, {10, 8}).isZero());

    EXPECT_TRUE(MakeEllipseGeometry({0, 0}, {0, 0}).isZero());
    EXPECT_FALSE(MakeEllipseGeometry({0, 0}, {0, 6}).isZero());
}
