#pragma once

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/VectorNetwork.h"
#include "MotionStudio/model/MaskMode.h"

namespace motion {

// One path mask evaluated at a point in time (layer-local path).
struct EvaluatedMask {
    // Compiled fill faces for clipping / rendering.
    BezierPath path;
    // Authoring network (empty when mask was not a VectorNetwork).
    VectorNetwork network;
    MaskMode mode = MaskMode::Add;
    float opacity = 1.0f;
    bool inverted = false;
    float feather = 0.0f;
    float expansion = 0.0f;
};

}  // namespace motion
