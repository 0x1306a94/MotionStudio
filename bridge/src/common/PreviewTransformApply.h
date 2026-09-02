#pragma once

#include <unordered_map>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/render/SceneState.h"

namespace bridge {

// Post-multiplies each matching layer's worldTransform by preview[id].
// effectiveWorld = worldTransform * L. Layers without an entry are unchanged.
void ApplyPreviewTransformsToScene(motion::SceneState &state, const std::unordered_map<motion::EntityId, motion::Mat3> &preview);

}  // namespace bridge
