#include <gtest/gtest.h>

#include "MotionStudio/common/PathGeometryEdit.h"

using motion::AppendVertex;
using motion::ApproxEqual;
using motion::BezierPath;
using motion::ClosePath;
using motion::InsertVertexOnSegment;
using motion::MoveInTangent;
using motion::MoveOutTangent;
using motion::MoveVertex;
using motion::RecenterPath;
using motion::RemoveVertex;
using motion::ToggleVertexSmooth;
using motion::Vec2;

namespace {

BezierPath MakeOpenLine() {
    BezierPath path;
    path.vertices.push_back({{0, 0}, {}, {}});
    path.vertices.push_back({{10, 0}, {}, {}});
    return path;
}

BezierPath MakeSmoothOpen() {
    BezierPath path;
    path.vertices.push_back({{0, 0}, {}, {2, 0}});
    path.vertices.push_back({{10, 0}, {-2, 0}, {}});
    return path;
}

}  // namespace

TEST(PathGeometryEditTest, MoveVertexKeepsRelativeTangentsWhenLinked) {
    BezierPath path = MakeSmoothOpen();
    BezierPath result = MoveVertex(std::move(path), 0, {1, 1}, true);
    EXPECT_TRUE(ApproxEqual(result.vertices[0].point, Vec2{1, 1}));
    EXPECT_TRUE(ApproxEqual(result.vertices[0].outTangent, Vec2{2, 0}));
}

TEST(PathGeometryEditTest, MoveVertexKeepsAbsoluteHandlesWhenUnlinked) {
    BezierPath path = MakeSmoothOpen();
    BezierPath result = MoveVertex(std::move(path), 0, {1, 1}, false);
    EXPECT_TRUE(ApproxEqual(result.vertices[0].point, Vec2{1, 1}));
    EXPECT_TRUE(ApproxEqual(result.vertices[0].outTangent, Vec2{1, -1}));
}

TEST(PathGeometryEditTest, MoveInTangentMirrorsOutWhenRequested) {
    BezierPath path = MakeSmoothOpen();
    BezierPath result = MoveInTangent(std::move(path), 1, {-3, 1}, true);
    EXPECT_TRUE(ApproxEqual(result.vertices[1].inTangent, Vec2{-3, 1}));
    EXPECT_TRUE(ApproxEqual(result.vertices[1].outTangent, Vec2{3, -1}));
}

TEST(PathGeometryEditTest, MoveOutTangentDoesNotMirrorWithoutFlag) {
    BezierPath path = MakeSmoothOpen();
    BezierPath result = MoveOutTangent(std::move(path), 0, {4, 2}, false);
    EXPECT_TRUE(ApproxEqual(result.vertices[0].outTangent, Vec2{4, 2}));
    EXPECT_TRUE(ApproxEqual(result.vertices[0].inTangent, Vec2{0, 0}));
}

TEST(PathGeometryEditTest, InsertVertexOnStraightSegmentAtMidpoint) {
    BezierPath result = InsertVertexOnSegment(MakeOpenLine(), 0, 0.5f);
    ASSERT_EQ(result.vertices.size(), 3u);
    EXPECT_TRUE(ApproxEqual(result.vertices[1].point, Vec2{5, 0}));
    // de Casteljau on a zero-tangent line yields colinear mid handles.
    EXPECT_TRUE(ApproxEqual(result.vertices[0].outTangent, Vec2{0, 0}));
    EXPECT_TRUE(ApproxEqual(result.vertices[1].inTangent, Vec2{-2.5f, 0}));
    EXPECT_TRUE(ApproxEqual(result.vertices[1].outTangent, Vec2{2.5f, 0}));
    EXPECT_TRUE(ApproxEqual(result.vertices[2].inTangent, Vec2{0, 0}));
}

TEST(PathGeometryEditTest, InsertVertexOnCubicPreservesEndPoints) {
    BezierPath path;
    path.vertices.push_back({{0, 0}, {}, {0, 6}});
    path.vertices.push_back({{10, 0}, {0, 6}, {}});
    BezierPath result = InsertVertexOnSegment(std::move(path), 0, 0.5f);
    ASSERT_EQ(result.vertices.size(), 3u);
    EXPECT_TRUE(ApproxEqual(result.vertices[0].point, Vec2{0, 0}));
    EXPECT_TRUE(ApproxEqual(result.vertices[2].point, Vec2{10, 0}));
    EXPECT_TRUE(ApproxEqual(result.vertices[1].point, Vec2{5, 4.5f}));
}

TEST(PathGeometryEditTest, RemoveVertexKeepsAtLeastTwoPoints) {
    BezierPath line = MakeOpenLine();
    BezierPath unchanged = RemoveVertex(line, 0);
    EXPECT_EQ(unchanged, line);

    BezierPath triangle;
    triangle.vertices.push_back({{0, 0}, {}, {}});
    triangle.vertices.push_back({{1, 0}, {}, {}});
    triangle.vertices.push_back({{0, 1}, {}, {}});
    BezierPath result = RemoveVertex(std::move(triangle), 1);
    ASSERT_EQ(result.vertices.size(), 2u);
    EXPECT_TRUE(ApproxEqual(result.vertices[0].point, Vec2{0, 0}));
    EXPECT_TRUE(ApproxEqual(result.vertices[1].point, Vec2{0, 1}));
}

TEST(PathGeometryEditTest, ClosePathSetsClosedFlag) {
    BezierPath result = ClosePath(MakeOpenLine());
    EXPECT_TRUE(result.closed);
    EXPECT_EQ(result.vertices.size(), 2u);
}

TEST(PathGeometryEditTest, AppendVertexAddsToOpenPath) {
    BezierPath result = AppendVertex(MakeOpenLine(), {{5, 5}, {}, {}});
    ASSERT_EQ(result.vertices.size(), 3u);
    EXPECT_TRUE(ApproxEqual(result.vertices[2].point, Vec2{5, 5}));
    EXPECT_FALSE(result.closed);
}

TEST(PathGeometryEditTest, AppendVertexNoOpWhenClosed) {
    BezierPath path = ClosePath(MakeOpenLine());
    BezierPath result = AppendVertex(path, {{5, 5}, {}, {}});
    EXPECT_EQ(result, path);
}

TEST(PathGeometryEditTest, InvalidIndexIsNoOp) {
    BezierPath path = MakeOpenLine();
    EXPECT_EQ(MoveVertex(path, 9, {1, 1}, true), path);
    EXPECT_EQ(RemoveVertex(path, 9), path);
    EXPECT_EQ(InsertVertexOnSegment(path, 9, 0.5f), path);
}

TEST(PathGeometryEditTest, ToggleVertexSmoothCornerToSmooth) {
    BezierPath path;
    path.vertices.push_back({{0, 0}, {}, {}});
    path.vertices.push_back({{10, 0}, {}, {}});
    path.vertices.push_back({{20, 0}, {}, {}});
    BezierPath result = ToggleVertexSmooth(std::move(path), 1);
    EXPECT_TRUE(ApproxEqual(result.vertices[1].inTangent, Vec2{-10.0f / 3.0f, 0}));
    EXPECT_TRUE(ApproxEqual(result.vertices[1].outTangent, Vec2{10.0f / 3.0f, 0}));
}

TEST(PathGeometryEditTest, ToggleVertexSmoothSmoothToCorner) {
    BezierPath path = MakeSmoothOpen();
    BezierPath result = ToggleVertexSmooth(std::move(path), 0);
    EXPECT_TRUE(ApproxEqual(result.vertices[0].inTangent, Vec2{0, 0}));
    EXPECT_TRUE(ApproxEqual(result.vertices[0].outTangent, Vec2{0, 0}));
}

TEST(PathGeometryEditTest, RecenterPathMovesBoundsCenterToOrigin) {
    BezierPath path;
    path.vertices.push_back({{10, 20}, {}, {1, 0}});
    path.vertices.push_back({{30, 40}, {}, {}});
    Vec2 center{};
    BezierPath result = RecenterPath(std::move(path), center);
    EXPECT_TRUE(ApproxEqual(center, Vec2{20, 30}));
    EXPECT_TRUE(ApproxEqual(result.vertices[0].point, Vec2{-10, -10}));
    EXPECT_TRUE(ApproxEqual(result.vertices[1].point, Vec2{10, 10}));
    // Relative tangents are preserved.
    EXPECT_TRUE(ApproxEqual(result.vertices[0].outTangent, Vec2{1, 0}));
}

TEST(PathGeometryEditTest, RecenterPathNoOpWhenAlreadyCentered) {
    BezierPath path = MakeOpenLine();
    path.vertices[0].point = {-5, 0};
    path.vertices[1].point = {5, 0};
    Vec2 center{1, 1};
    BezierPath result = RecenterPath(path, center);
    EXPECT_TRUE(ApproxEqual(center, Vec2{0, 0}));
    EXPECT_EQ(result, path);
}
