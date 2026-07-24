#pragma once

#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/render/DrawCommand.h"
#include "MotionStudio/render/SceneState.h"

namespace motion {

// Flattens an evaluated scene snapshot into a draw command list. Each layer
// becomes Save / SetOpacity / SetBlendMode / per-item DrawPath|StrokePath /
// Restore. Viewport size and background color are not part of the command
// list; the caller passes them to RenderAdapter::beginFrame.
// state: evaluated scene snapshot.
DrawCommandList BuildCommands(const SceneState &state);

// Builds preview-only selection chrome as scene-space outline commands.
// selectedLayerIds: selected layer IDs; missing or invisible layers are skipped.
// strokeWidth: outline stroke width in scene units.
DrawCommandList BuildSelectionOutlineCommands(const SceneState &state,
                                              const std::vector<EntityId> &selectedLayerIds,
                                              float strokeWidth);

}  // namespace motion
