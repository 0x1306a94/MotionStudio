#pragma once

#include <optional>

#include "MotionStudio/animation/Easing.h"
#include "MotionStudio/common/Math.h"
#include "MotionStudio/common/Time.h"

namespace motion {

// 关键帧：time 处的属性值 + 到下一关键帧的插值方式。
template <typename T>
struct Keyframe {
    FrameTime time = 0;
    T value{};
    Easing easing = Easing::Linear();

    // 空间手柄（仅 Vec2 类型使用）：相对 value 的偏移，决定运动路径的弧度。
    std::optional<Vec2> spatialInTangent;
    std::optional<Vec2> spatialOutTangent;

    bool operator==(const Keyframe& other) const {
        return time == other.time && value == other.value && easing == other.easing &&
               spatialInTangent == other.spatialInTangent &&
               spatialOutTangent == other.spatialOutTangent;
    }
    bool operator!=(const Keyframe& other) const { return !(*this == other); }
};

}  // namespace motion
