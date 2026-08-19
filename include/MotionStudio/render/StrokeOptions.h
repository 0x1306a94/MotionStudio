#pragma once

#include <vector>

#include "MotionStudio/model/LineCap.h"
#include "MotionStudio/model/LineJoin.h"
#include "MotionStudio/model/StrokeMode.h"
#include "MotionStudio/model/StrokePosition.h"

namespace motion {

// How a stroke is drawn along a path: pen geometry, alignment relative to
// the path edge, the visible segment window (trim), and optional dash pattern.
struct StrokeOptions {
    float width = 0;
    LineCap cap = LineCap::Butt;
    LineJoin join = LineJoin::Miter;
    float miterLimit = 4.0f;
    StrokePosition position = StrokePosition::Center;
    float trimStart = 0;   // 0.0 ~ 1.0 fraction of path length
    float trimEnd = 1;     // 0.0 ~ 1.0 fraction of path length
    float trimOffset = 0;  // degrees, rotates the trim window
    StrokeMode strokeMode = StrokeMode::Solid;
    std::vector<float> dashes;
    float dashOffset = 0;
};

}  // namespace motion
