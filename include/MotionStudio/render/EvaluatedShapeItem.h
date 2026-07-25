#pragma once

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/render/Paint.h"
#include "MotionStudio/render/StrokeOptions.h"

namespace motion {

// One flattened draw primitive produced by scene evaluation.
struct EvaluatedShapeItem {
    BezierPath path;  // world-space path, all shape transforms applied
    Paint paint;
    bool isStroke = false;
    StrokeOptions stroke;
};

}  // namespace motion
