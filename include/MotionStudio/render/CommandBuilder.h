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

// Builds preview-only selection chrome (oriented box, scale handles, optional
// anchor) for selectedLayerIds. primaryLayerId is the AE primary selection; when
// invalid, the last id in selectedLayerIds is used.
// strokeWidth / handleSize: chrome sizes in scene units.
// showAnchor: false omits the anchor crosshair.
DrawCommandList BuildSelectionOutlineCommands(const SceneState &state,
                                              const std::vector<EntityId> &selectedLayerIds,
                                              EntityId primaryLayerId,
                                              float strokeWidth,
                                              float handleSize,
                                              bool showAnchor = true);

}  // namespace motion
