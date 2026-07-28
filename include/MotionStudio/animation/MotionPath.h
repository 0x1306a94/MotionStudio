#pragma once

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Vec2.h"

namespace motion {

// Builds an open BezierPath approximating the spatial motion of a Vec2
// animatable (typically transform.position). Each keyframe becomes a vertex;
// a segment uses cubic spatial tangents when both endpoints provide them,
// otherwise the segment is a straight line (zero tangents).
// Fewer than two keyframes yields an empty path.
BezierPath BuildMotionPath(const Animatable<Vec2> &position);

}  // namespace motion
