#pragma once

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Expected.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/render/SceneState.h"

namespace motion {

class Document;

// Walks a composition at a given time and flattens it into a SceneState:
// world transforms, parent chains, opacity inheritance, in/outPoint clipping,
// ShapeGroup expansion and precomp recursion with time remapping.
class SceneEvaluator {
public:
    // Evaluates a composition into an immutable scene snapshot.
    // document: the owning document (provides the entity index).
    // compositionId: id of the composition to evaluate.
    // time: frame time in the composition timeline.
    static Expected<SceneState> Evaluate(const Document& document,
                                         EntityId compositionId, FrameTime time);
};

}  // namespace motion
