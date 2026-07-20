#pragma once

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/model/LineCap.h"
#include "MotionStudio/model/LineJoin.h"
#include "MotionStudio/render/Paint.h"

namespace motion {

// One flattened draw primitive produced by scene evaluation.
struct EvaluatedShapeItem {
    BezierPath path;  // world-space path, all shape transforms applied
    Paint paint;
    bool isStroke = false;
    float strokeWidth = 0;
    LineCap cap = LineCap::Butt;
    LineJoin join = LineJoin::Miter;
};

}  // namespace motion
