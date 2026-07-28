#pragma once

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/EntityId.h"

namespace motion {

// Constraint that drives a layer's position (and optional rotation) from
// another layer's evaluated ShapePath / baked geometry path.
struct FollowPath {
    bool enabled = false;
    // Layer whose path is followed. Invalid id means unbound.
    EntityId pathLayerId;
    // Fraction of path arc length in [0, 1] (clamped at evaluate time).
    Animatable<float> pathOffset{0.0f};
    // When true, rotation is overridden by path tangent + orientOffset.
    bool orientAlongPath = true;
    // Degrees added to the path tangent angle when orientAlongPath is true.
    Animatable<float> orientOffset{0.0f};
};

}  // namespace motion
