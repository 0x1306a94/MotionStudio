#include "CommandHelpers.h"

#include <utility>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/model/Composition.h"

namespace motion {

namespace {

// 值类型与属性类型相符时设置静态值；oldValue 输出设置前的值。
template <typename T>
bool applyStaticValue(Animatable<T>* animatable, const PropertyValue& newValue,
                      PropertyValue& oldValue) {
    const T* typed = std::get_if<T>(&newValue);
    if (!typed) {
        return false;
    }
    oldValue = animatable->staticValue();
    animatable->setStaticValue(*typed);
    return true;
}

template <typename T>
std::optional<KeyframeData> takeKeyframeTyped(Animatable<T>* animatable, FrameTime time) {
    auto keyframe = animatable->takeKeyframe(time);
    if (!keyframe) {
        return std::nullopt;
    }
    return KeyframeData{std::move(*keyframe)};
}

template <typename T>
bool applyEasing(Animatable<T>* animatable, FrameTime time, const Easing& easing,
                 Easing* oldEasingOut) {
    for (const Keyframe<T>& keyframe : animatable->keyframes()) {
        if (keyframe.time != time) {
            continue;
        }
        if (oldEasingOut) {
            *oldEasingOut = keyframe.easing;
        }
        Keyframe<T> updated = keyframe;
        updated.easing = easing;
        return animatable->updateKeyframe(time, std::move(updated));
    }
    return false;  // 该类型上无此帧
}

}  // namespace

bool applyStaticValueAny(AnimatableBase* target, const PropertyValue& newValue,
                         PropertyValue& oldValue) {
    switch (target->valueType()) {
        case AnimatableType::Float:
            return applyStaticValue(static_cast<Animatable<float>*>(target), newValue,
                                    oldValue);
        case AnimatableType::Vec2:
            return applyStaticValue(static_cast<Animatable<Vec2>*>(target), newValue,
                                    oldValue);
        case AnimatableType::Color:
            return applyStaticValue(static_cast<Animatable<Color>*>(target), newValue,
                                    oldValue);
        case AnimatableType::BezierPath:
            return applyStaticValue(static_cast<Animatable<BezierPath>*>(target),
                                    newValue, oldValue);
        case AnimatableType::String:
            return applyStaticValue(static_cast<Animatable<std::string>*>(target),
                                    newValue, oldValue);
    }
    return false;
}

std::optional<KeyframeData> takeKeyframeAny(AnimatableBase* target, FrameTime time) {
    switch (target->valueType()) {
        case AnimatableType::Float:
            return takeKeyframeTyped(static_cast<Animatable<float>*>(target), time);
        case AnimatableType::Vec2:
            return takeKeyframeTyped(static_cast<Animatable<Vec2>*>(target), time);
        case AnimatableType::Color:
            return takeKeyframeTyped(static_cast<Animatable<Color>*>(target), time);
        case AnimatableType::BezierPath:
            return takeKeyframeTyped(static_cast<Animatable<BezierPath>*>(target), time);
        case AnimatableType::String:
            return takeKeyframeTyped(static_cast<Animatable<std::string>*>(target), time);
    }
    return std::nullopt;
}

void addKeyframeAny(AnimatableBase* target, const KeyframeData& keyframe) {
    // variant 备选顺序：float / Vec2 / Color / BezierPath / std::string。
    // 关键帧类型与属性类型不符时静默跳过。
    switch (keyframe.index()) {
        case 0:
            if (target->valueType() == AnimatableType::Float) {
                static_cast<Animatable<float>*>(target)->addKeyframe(std::get<0>(keyframe));
            }
            break;
        case 1:
            if (target->valueType() == AnimatableType::Vec2) {
                static_cast<Animatable<Vec2>*>(target)->addKeyframe(std::get<1>(keyframe));
            }
            break;
        case 2:
            if (target->valueType() == AnimatableType::Color) {
                static_cast<Animatable<Color>*>(target)->addKeyframe(std::get<2>(keyframe));
            }
            break;
        case 3:
            if (target->valueType() == AnimatableType::BezierPath) {
                static_cast<Animatable<BezierPath>*>(target)->addKeyframe(
                    std::get<3>(keyframe));
            }
            break;
        default:
            if (target->valueType() == AnimatableType::String) {
                static_cast<Animatable<std::string>*>(target)->addKeyframe(
                    std::get<4>(keyframe));
            }
            break;
    }
}

FrameTime keyframeTime(const KeyframeData& keyframe) {
    switch (keyframe.index()) {
        case 0: return std::get<0>(keyframe).time;
        case 1: return std::get<1>(keyframe).time;
        case 2: return std::get<2>(keyframe).time;
        case 3: return std::get<3>(keyframe).time;
        default: return std::get<4>(keyframe).time;
    }
}

void setKeyframeTime(KeyframeData& keyframe, FrameTime time) {
    switch (keyframe.index()) {
        case 0: std::get<0>(keyframe).time = time; break;
        case 1: std::get<1>(keyframe).time = time; break;
        case 2: std::get<2>(keyframe).time = time; break;
        case 3: std::get<3>(keyframe).time = time; break;
        default: std::get<4>(keyframe).time = time; break;
    }
}

bool applyEasingAny(AnimatableBase* target, FrameTime time, const Easing& easing,
                    Easing* oldEasingOut) {
    switch (target->valueType()) {
        case AnimatableType::Float:
            return applyEasing(static_cast<Animatable<float>*>(target), time, easing,
                               oldEasingOut);
        case AnimatableType::Vec2:
            return applyEasing(static_cast<Animatable<Vec2>*>(target), time, easing,
                               oldEasingOut);
        case AnimatableType::Color:
            return applyEasing(static_cast<Animatable<Color>*>(target), time, easing,
                               oldEasingOut);
        case AnimatableType::BezierPath:
            return applyEasing(static_cast<Animatable<BezierPath>*>(target), time,
                               easing, oldEasingOut);
        case AnimatableType::String:
            return applyEasing(static_cast<Animatable<std::string>*>(target), time,
                               easing, oldEasingOut);
    }
    return false;
}

int indexOfLayer(const Composition& composition, EntityId layerId) {
    for (size_t i = 0; i < composition.layers.size(); ++i) {
        if (composition.layers[i]->id == layerId) {
            return int(i);
        }
    }
    return -1;
}

}  // namespace motion
