#pragma once

#include <optional>
#include <vector>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/common/Vec2.h"

namespace motion {

class Document;
class Layer;

// Position / rotation overrides produced by an active Follow Path constraint.
struct FollowSample {
    Vec2 parentSpacePosition = {};
    float rotationDegrees = 0.0f;
    bool overrideRotation = false;
};

// Evaluates the Shape layer geometry to a BezierPath at time.
// Path uses the animatable path; Rect/Ellipse are baked; TrimPath / missing
// geometry yield nullopt.
std::optional<BezierPath> EvaluateLayerPath(const Layer &layer, PreviewTime time);

// Evaluates Follow Path for layer when enabled and resolvable.
std::optional<FollowSample> EvaluateFollowPath(const Document &document, const Layer &layer,
                                               PreviewTime time, const Mat3 &context,
                                               const Mat3 &parentWorld,
                                               std::vector<EntityId> &followVisiting);

// World transform including Follow Path overrides on local matrices.
// parentVisiting: guards parent-chain cycles.
// followVisiting: guards Follow Path mutual cycles.
Mat3 FollowAwareWorldTransform(const Document &document, const Layer &layer, PreviewTime time,
                               const Mat3 &context, std::vector<EntityId> &parentVisiting,
                               std::vector<EntityId> &followVisiting);

}  // namespace motion
