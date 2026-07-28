#pragma once

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Vec2.h"

namespace motion {

// Sample of a BezierPath at a given arc-length position.
struct PathSample {
    Vec2 point = {};
    // Direction along the path at point (may be unnormalized). Zero-length
    // paths use (1, 0).
    Vec2 tangent = {};
};

// Approximate total arc length of path (polyline samples per cubic segment).
// path: Bezier path in layer-local space.
float PathArcLength(const BezierPath &path);

// Point and tangent at the given arc length along path.
// Arc length is clamped to [0, PathArcLength(path)]. Empty or zero-length
// paths return the first vertex (or origin) with tangent (1, 0).
// path: Bezier path in layer-local space.
// arcLength: distance along the path from the start vertex.
PathSample PointAndTangentAtArcLength(const BezierPath &path, float arcLength);

}  // namespace motion
