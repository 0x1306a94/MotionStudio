#pragma once

#include <optional>

#include "MotionStudio/animation/Easing.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/common/Vec2.h"

namespace motion {

// Keyframe: property value at a given time, plus the easing used to
// interpolate towards the next keyframe.
template <typename T>
struct Keyframe {
    // Frame time at which this keyframe is placed.
    FrameTime time = 0;
    // Property value at this keyframe.
    T value{};
    // Easing curve applied from this keyframe to the next.
    Easing easing = Easing::Linear();

    // Spatial tangent handles (used only for Vec2 properties): offsets relative
    // to value that shape the arc of the motion path between keyframes.
    std::optional<Vec2> spatialInTangent;
    std::optional<Vec2> spatialOutTangent;

    bool operator==(const Keyframe &other) const {
        return time == other.time && value == other.value && easing == other.easing &&
            spatialInTangent == other.spatialInTangent &&
            spatialOutTangent == other.spatialOutTangent;
    }
    bool operator!=(const Keyframe &other) const {
        return !(*this == other);
    }
};

}  // namespace motion
