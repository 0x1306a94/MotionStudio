#pragma once

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/EntityId.h"

namespace motion {

// Point-text layout along another layer's evaluated path (PAG TextPathOptions).
// Core stores parameters only; arc-length layout lives in the adapter.
struct TextPath {
    bool enabled = false;
    // Path source layer in the same composition. Invalid id means unbound.
    EntityId pathLayerId;
    bool reversed = false;
    // Align glyphs perpendicular to the path tangent (PAG default).
    bool perpendicular = true;
    bool forceAlignment = false;
    Animatable<float> firstMargin{0.f};
    Animatable<float> lastMargin{0.f};
};

}  // namespace motion
