#pragma once

#include "MotionStudio/common/Color.h"
#include "MotionStudio/model/FillRule.h"

namespace motion {

// Solid-color paint (M3 first version). Gradients will extend this later.
struct Paint {
    Color color;
    FillRule fillRule = FillRule::NonZero;
};

}  // namespace motion
