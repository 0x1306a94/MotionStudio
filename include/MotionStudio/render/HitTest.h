#pragma once

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/render/EvaluatedLayer.h"
#include "MotionStudio/render/SceneState.h"

namespace motion {

// Returns true when point is inside any filled shape or close enough to any
// stroked shape in the already-evaluated, world-space layer geometry.
bool HitTestLayer(const EvaluatedLayer &layer, Vec2 point, float tolerance);

// Returns the axis-aligned scene-space bounds of the evaluated layer geometry.
bool BoundsOfLayer(const EvaluatedLayer &layer, Vec2 &minPoint, Vec2 &maxPoint);

// Returns the topmost layer containing point, or an invalid EntityId if none hit.
EntityId HitTestLayerAtPoint(const SceneState &state, Vec2 point, float tolerance);

}  // namespace motion
