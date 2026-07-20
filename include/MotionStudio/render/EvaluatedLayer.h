#pragma once

#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/render/EvaluatedShapeItem.h"

namespace motion {

// A visible layer evaluated at one point in time. ShapeGroups are already
// expanded into shapeItems; precomp layers are flattened into the layer list.
struct EvaluatedLayer {
    EntityId id;  // kept for UI hit-testing
    Mat3 worldTransform;
    float opacity = 1;  // inherited from ancestors
    BlendMode blendMode = BlendMode::Normal;
    std::vector<EvaluatedShapeItem> shapeItems;
};

}  // namespace motion
