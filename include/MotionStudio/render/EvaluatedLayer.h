#pragma once

#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/TrackMatteType.h"
#include "MotionStudio/render/EvaluatedMask.h"
#include "MotionStudio/render/EvaluatedShapeItem.h"

namespace motion {

// A visible layer evaluated at one point in time. Paths in shapeItems are
// layer-local; precomp layers are flattened into the layer list.
struct EvaluatedLayer {
    EntityId id;  // kept for UI hit-testing
    Mat3 worldTransform;
    // World-space location of transform.anchorPoint (parent * position).
    Vec2 worldAnchor;
    float opacity = 1;  // inherited from ancestors
    BlendMode blendMode = BlendMode::Normal;
    std::vector<EvaluatedShapeItem> shapeItems;
    std::vector<EvaluatedMask> masks;
    TrackMatteType trackMatteType = TrackMatteType::None;
    EntityId matteSourceId;
    // True when another evaluated layer uses this layer as its track matte;
    // CommandBuilder skips normal compositing for these layers.
    bool usedAsMatteOnly = false;
};

}  // namespace motion
