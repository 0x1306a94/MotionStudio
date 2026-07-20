#pragma once

#include <cstddef>

#include "MotionStudio/common/BezierPath.h"

namespace motion {

// Resamples the path to exactly vertexCount vertices distributed by arc length.
// New vertices are placed on the Bezier segments via de Casteljau splitting,
// so the shape is preserved (up to the flattening tolerance).
// path: source path; vertexCount: desired vertex count.
// Returns the path unchanged when it has fewer than 2 vertices, already has
// vertexCount vertices, vertexCount is less than 2, or the path is degenerate
// (zero arc length).
BezierPath ResamplePath(const BezierPath &path, size_t vertexCount);

}  // namespace motion
