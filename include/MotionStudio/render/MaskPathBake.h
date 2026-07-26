#pragma once

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Time.h"

namespace motion {

class Layer;

// Bakes the layer's shape content at `time` into a closed BezierPath for a new
// path mask. Snapshot only — later shape edits do not update the mask.
// Falls back to a 200x200 centered rectangle when the layer has no usable
// geometry (non-shape layers, missing geometry, empty path).
BezierPath BakeMaskPathFromLayer(const Layer &layer, FrameTime time);

}  // namespace motion
