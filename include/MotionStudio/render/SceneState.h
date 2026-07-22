#pragma once

#include <vector>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/render/EvaluatedLayer.h"

namespace motion {

// Immutable value snapshot of one evaluated frame, fully detached from the
// Document after evaluation. Safe to pass across threads.
struct SceneState {
    std::vector<EvaluatedLayer> layers;  // render order, bottom to top
    int viewportWidth = 0;
    int viewportHeight = 0;
    Color backgroundColor;
    float cornerRadius = 0.0f;
};

}  // namespace motion
