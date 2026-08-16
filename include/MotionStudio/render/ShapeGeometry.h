#pragma once

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Vec2.h"

namespace motion {

// Kind of evaluated draw geometry. Rect/Ellipse stay parametric until the
// render adapter so backends can use addRect/addOval fast paths.
enum class ShapeGeometryKind {
    Path,
    Rect,
    Ellipse,
};

// Layer-local geometry snapshot. World placement is applied via the layer
// transform (or ConcatTransform commands), not baked into these fields.
struct ShapeGeometry {
    ShapeGeometryKind kind = ShapeGeometryKind::Path;

    // Meaningful when kind == Path. Fill faces (CompileFillFaces).
    BezierPath path;

    // Meaningful when kind == Path. All network edges for stroking
    // (CompileStrokeEdges). Empty means stroke falls back to `path`.
    BezierPath strokePath;

    // Meaningful when kind == Rect or Ellipse (center + size).
    Vec2 center{};
    Vec2 size{};

    // Meaningful when kind == Rect. Values > 0 produce a rounded rectangle.
    float cornerRadius = 0;

    // True when geometry collapses to a point (width and height both zero).
    // Path uses BezierPath::isZero on fill and stroke; Rect/Ellipse use size.
    // Hairlines return false.
    bool isZero() const;
};

// Builds a Path geometry from an authoring BezierPath.
ShapeGeometry MakePathGeometry(BezierPath path);

// Builds a centered Rect geometry.
// center: rectangle center in layer-local space.
// size: width/height.
// cornerRadius: corner radius clamped by the adapter/evaluator consumers.
ShapeGeometry MakeRectGeometry(Vec2 center, Vec2 size, float cornerRadius = 0);

// Builds a centered Ellipse geometry.
// center: ellipse center in layer-local space.
// size: width/height of the bounding box.
ShapeGeometry MakeEllipseGeometry(Vec2 center, Vec2 size);

// Returns true for closed contours (Rect/Ellipse always; Path uses path.closed).
bool ShapeGeometryIsClosed(const ShapeGeometry &geometry);

// Expands parametric geometry into a BezierPath for hit-testing and other
// path-only consumers. Rect/Ellipse match the previous SceneEvaluator expansion.
// For Path kind returns fill faces (`path`), not stroke edges.
BezierPath ShapeGeometryToBezierPath(const ShapeGeometry &geometry);

// Path to stroke: strokePath when non-empty, otherwise the fill/parametric path.
BezierPath ShapeGeometryStrokePath(const ShapeGeometry &geometry);

}  // namespace motion
