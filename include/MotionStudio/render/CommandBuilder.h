#pragma once

#include "MotionStudio/render/DrawCommand.h"
#include "MotionStudio/render/SceneState.h"

namespace motion {

// Flattens an evaluated scene snapshot into a draw command list. Each layer
// becomes Save / SetOpacity / SetBlendMode / per-item DrawPath|StrokePath /
// Restore. Viewport size and background color are not part of the command
// list; the caller passes them to RenderAdapter::beginFrame.
// state: evaluated scene snapshot.
DrawCommandList BuildCommands(const SceneState &state);

}  // namespace motion
