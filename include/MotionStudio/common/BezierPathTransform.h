#pragma once

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Mat3.h"

namespace motion {

// Transforms path vertices into another space.
// Points use matrix * point; in/out tangents use matrix.transformVector (no translation).
BezierPath TransformBezierPath(const BezierPath &path, const Mat3 &matrix);

}  // namespace motion
