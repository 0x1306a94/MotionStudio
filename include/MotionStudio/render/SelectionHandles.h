#pragma once

#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/render/DrawCommand.h"
#include "MotionStudio/render/SceneState.h"

namespace motion {

// Hit target on selection chrome, ordered by pick priority when testing.
enum class SelectionHandleKind {
    None = 0,
    Anchor,
    ScaleCorner0,
    ScaleCorner1,
    ScaleCorner2,
    ScaleCorner3,
    ScaleEdge0,
    ScaleEdge1,
    ScaleEdge2,
    ScaleEdge3,
    Rotate0,
    Rotate1,
    Rotate2,
    Rotate3,
};

// Scene-space free-transform chrome for the current selection.
// corners: TL, TR, BR, BL. edgeMids follow the same edge order (top, right,
// bottom, left). primaryLayerId selects which layer owns the anchor handle.
struct SelectionHandles {
    bool valid = false;
    // True when the box follows a single layer's world transform (OBB).
    // False for multi-select axis-aligned union boxes.
    bool isOriented = false;
    Vec2 corners[4] = {};
    Vec2 edgeMids[4] = {};
    Vec2 center = {};
    Vec2 anchor = {};
    EntityId primaryLayerId;
    // Orientation of the box edges in degrees (counter-clockwise from +X).
    float boxRotationDegrees = 0;
    // Layer-local AABB of the primary (or sole) oriented selection; unused
    // when isOriented is false.
    Vec2 localMin = {};
    Vec2 localMax = {};
};

// Builds selection handles for selectedLayerIds. primaryLayerId is the AE
// primary selection (last selected); when invalid, the last valid id in
// selectedLayerIds is used. Returns false when no selected layer has bounds.
bool BuildSelectionHandles(const SceneState &state,
                           const std::vector<EntityId> &selectedLayerIds,
                           EntityId primaryLayerId,
                           SelectionHandles &out);

// Picks the topmost handle under scenePoint. handleHitRadius is the scale /
// anchor hit radius in scene units. Rotate zones lie outside each corner
// between rotateInner and rotateOuter (scene units from the corner).
SelectionHandleKind HitTestSelectionHandle(const SelectionHandles &handles,
                                           Vec2 scenePoint,
                                           float handleHitRadius,
                                           float rotateInner,
                                           float rotateOuter);

// Draws the selection box, scale handles, and optionally the primary anchor.
// strokeWidth / handleSize are in scene units (typically derived from view points).
// showAnchor: false hides the anchor crosshair.
DrawCommandList BuildSelectionHandleCommands(const SelectionHandles &handles,
                                             float strokeWidth,
                                             float handleSize,
                                             bool showAnchor = true);

}  // namespace motion
