#include "CommandHelpers.h"

#include <type_traits>
#include <utility>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/model/Composition.h"

namespace motion {

namespace {

template <typename T>
bool applyStaticValue(AnimatableBase* target, const PropertyValue& newValue,
                      PropertyValue& oldValue) {
    auto* animatable = dynamic_cast<Animatable<T>*>(target);
    if (!animatable) {
        return false;
    }
    oldValue = animatable->staticValue();
    return std::visit(
        [&](const auto& typed) {
            using ValueType = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<ValueType, T>) {
                animatable->setStaticValue(typed);
                return true;
            } else {
                return false;  // 值类型与属性类型不符
            }
        },
        newValue);
}

template <typename T>
bool applyEasing(AnimatableBase* target, FrameTime time, const Easing& easing,
                 Easing* oldEasingOut) {
    auto* animatable = dynamic_cast<Animatable<T>*>(target);
    if (!animatable) {
        return false;
    }
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
    if (applyStaticValue<float>(target, newValue, oldValue)) {
        return true;
    }
    if (applyStaticValue<Vec2>(target, newValue, oldValue)) {
        return true;
    }
    if (applyStaticValue<Color>(target, newValue, oldValue)) {
        return true;
    }
    if (applyStaticValue<BezierPath>(target, newValue, oldValue)) {
        return true;
    }
    return applyStaticValue<std::string>(target, newValue, oldValue);
}

std::optional<KeyframeData> takeKeyframeAny(AnimatableBase* target, FrameTime time) {
    if (auto* animatable = dynamic_cast<Animatable<float>*>(target)) {
        auto keyframe = animatable->takeKeyframe(time);
        if (!keyframe) {
            return std::nullopt;
        }
        return KeyframeData{std::move(*keyframe)};
    }
    if (auto* animatable = dynamic_cast<Animatable<Vec2>*>(target)) {
        auto keyframe = animatable->takeKeyframe(time);
        if (!keyframe) {
            return std::nullopt;
        }
        return KeyframeData{std::move(*keyframe)};
    }
    if (auto* animatable = dynamic_cast<Animatable<Color>*>(target)) {
        auto keyframe = animatable->takeKeyframe(time);
        if (!keyframe) {
            return std::nullopt;
        }
        return KeyframeData{std::move(*keyframe)};
    }
    if (auto* animatable = dynamic_cast<Animatable<BezierPath>*>(target)) {
        auto keyframe = animatable->takeKeyframe(time);
        if (!keyframe) {
            return std::nullopt;
        }
        return KeyframeData{std::move(*keyframe)};
    }
    if (auto* animatable = dynamic_cast<Animatable<std::string>*>(target)) {
        auto keyframe = animatable->takeKeyframe(time);
        if (!keyframe) {
            return std::nullopt;
        }
        return KeyframeData{std::move(*keyframe)};
    }
    return std::nullopt;
}

void addKeyframeAny(AnimatableBase* target, const KeyframeData& keyframe) {
    std::visit(
        [&](const auto& typed) {
            using ValueType = std::decay_t<decltype(typed.value)>;
            auto* animatable = dynamic_cast<Animatable<ValueType>*>(target);
            if (animatable) {
                animatable->addKeyframe(typed);
            }
        },
        keyframe);
}

FrameTime keyframeTime(const KeyframeData& keyframe) {
    return std::visit([](const auto& typed) { return typed.time; }, keyframe);
}

void setKeyframeTime(KeyframeData& keyframe, FrameTime time) {
    std::visit([&](auto& typed) { typed.time = time; }, keyframe);
}

bool applyEasingAny(AnimatableBase* target, FrameTime time, const Easing& easing,
                    Easing* oldEasingOut) {
    if (auto* animatable = dynamic_cast<Animatable<float>*>(target)) {
        return applyEasing<float>(animatable, time, easing, oldEasingOut);
    }
    if (auto* animatable = dynamic_cast<Animatable<Vec2>*>(target)) {
        return applyEasing<Vec2>(animatable, time, easing, oldEasingOut);
    }
    if (auto* animatable = dynamic_cast<Animatable<Color>*>(target)) {
        return applyEasing<Color>(animatable, time, easing, oldEasingOut);
    }
    if (auto* animatable = dynamic_cast<Animatable<BezierPath>*>(target)) {
        return applyEasing<BezierPath>(animatable, time, easing, oldEasingOut);
    }
    if (auto* animatable = dynamic_cast<Animatable<std::string>*>(target)) {
        return applyEasing<std::string>(animatable, time, easing, oldEasingOut);
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
