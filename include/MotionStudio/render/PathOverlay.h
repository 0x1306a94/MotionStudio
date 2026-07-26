#pragma once

#include <vector>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/render/DrawCommand.h"
#include "MotionStudio/render/SceneState.h"

namespace motion {

// One preview-only path stroke in the editor chrome layer (masks, pen tool, …).
// path is in the local space of worldTransform.
struct PathOverlayItem {
    Mat3 worldTransform = Mat3::Identity();
    BezierPath path;
    Color color;
};

// Flattens overlay items into StrokePath commands (Save / ConcatTransform /
// StrokePath / Restore per item). Empty paths are skipped.
// items: overlay paths to stroke.
// strokeWidth: stroke width in scene units.
DrawCommandList BuildPathOverlayCommands(const std::vector<PathOverlayItem> &items,
                                         float strokeWidth);

// Builds overlay items for every path mask on selected layers (layer-local
// path × layer world transform). Missing layer ids are ignored.
// state: evaluated scene snapshot.
// selectedLayerIds: layers whose masks should be outlined.
// color: stroke color for all collected masks.
std::vector<PathOverlayItem> CollectMaskPathOverlays(
    const SceneState &state, const std::vector<EntityId> &selectedLayerIds, Color color);

}  // namespace motion
