#pragma once

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/render/EvaluatedLayer.h"
#include "MotionStudio/render/SceneState.h"

namespace motion {

// Returns true when point (scene space) hits any filled/stroked shape on the
// layer. Layer-local paths are transformed by EvaluatedLayer::worldTransform.
bool HitTestLayer(const EvaluatedLayer &layer, Vec2 point, float tolerance);

// Returns the axis-aligned scene-space bounds of the evaluated layer geometry.
bool BoundsOfLayer(const EvaluatedLayer &layer, Vec2 &minPoint, Vec2 &maxPoint);

// Returns the axis-aligned layer-local bounds of the evaluated layer geometry.
bool BoundsOfLayerLocal(const EvaluatedLayer &layer, Vec2 &minPoint, Vec2 &maxPoint);

// Union of descendant geometry in container local space. Used for Group
// selection chrome and inspector size. containerId: the parent layer id.
bool BoundsOfDescendantUnionLocal(const SceneState &state, EntityId containerId, Vec2 &minPoint, Vec2 &maxPoint);

// Own geometry bounds, or descendant union mapped to scene space when the
// layer has no drawable items (Group).
bool BoundsOfLayerIncludingDescendants(const SceneState &state, const EvaluatedLayer &layer, Vec2 &minPoint,
                                       Vec2 &maxPoint);

// Returns the topmost layer containing point, or an invalid EntityId if none hit.
EntityId HitTestLayerAtPoint(const SceneState &state, Vec2 point, float tolerance);

}  // namespace motion
