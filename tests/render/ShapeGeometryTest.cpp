#include <gtest/gtest.h>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/render/ShapeGeometry.h"

using motion::BezierPath;
using motion::MakeEllipseGeometry;
using motion::MakePathGeometry;
using motion::MakeRectGeometry;
using motion::Vec2;

TEST(ShapeGeometryTest, IsZeroForPathRectEllipse) {
    BezierPath collapsed;
    collapsed.vertices.push_back({{1, 2}, {}, {}});
    collapsed.vertices.push_back({{1, 2}, {}, {}});
    EXPECT_TRUE(MakePathGeometry(collapsed).isZero());

    BezierPath hairline;
    hairline.vertices.push_back({{0, 0}, {}, {}});
    hairline.vertices.push_back({{10, 0}, {}, {}});
    EXPECT_FALSE(MakePathGeometry(hairline).isZero());

    EXPECT_TRUE(MakeRectGeometry({0, 0}, {0, 0}).isZero());
    EXPECT_FALSE(MakeRectGeometry({0, 0}, {10, 0}).isZero());
    EXPECT_FALSE(MakeRectGeometry({0, 0}, {10, 8}).isZero());

    EXPECT_TRUE(MakeEllipseGeometry({0, 0}, {0, 0}).isZero());
    EXPECT_FALSE(MakeEllipseGeometry({0, 0}, {0, 6}).isZero());
}
