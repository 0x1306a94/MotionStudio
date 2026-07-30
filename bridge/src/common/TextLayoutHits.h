#pragma once

#include "MotionStudio/render/SceneState.h"

namespace bridge {

// For autoHeight text layers, run TextLayout and write measuredSize into
// textItem.hitSize (preview/hit only; does not mutate the document model).
void ApplyAutoHeightTextHitSizes(motion::SceneState &state);

}  // namespace bridge
