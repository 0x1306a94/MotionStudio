#include "MotionStudio/animation/Animatable.h"

#include <algorithm>
#include <string>
#include <type_traits>
#include <utility>

#include "MotionStudio/animation/Interpolator.h"
#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Vec2.h"

namespace motion {

template <typename T>
Animatable<T>::Animatable(T staticValue) : value_(std::move(staticValue)) {}

template <typename T>
void Animatable<T>::addKeyframe(Keyframe<T> keyframe) {
    auto it = lowerBound(keyframe.time);
    if (it != keyframes_.end() && it->time == keyframe.time) {
        *it = std::move(keyframe);
    } else {
        keyframes_.insert(it, std::move(keyframe));
    }
}

template <typename T>
void Animatable<T>::removeKeyframe(FrameTime time) {
    auto it = find(time);
    if (it != keyframes_.end()) {
        keyframes_.erase(it);
    }
}

template <typename T>
std::optional<Keyframe<T>> Animatable<T>::takeKeyframe(FrameTime time) {
    auto it = find(time);
    if (it == keyframes_.end()) {
        return std::nullopt;
    }
    Keyframe<T> taken = std::move(*it);
    keyframes_.erase(it);
    return taken;
}

template <typename T>
bool Animatable<T>::updateKeyframe(FrameTime time, Keyframe<T> keyframe) {
    auto it = find(time);
    if (it == keyframes_.end()) {
        return false;
    }
    keyframe.time = time;
    *it = std::move(keyframe);
    return true;
}

template <typename T>
void Animatable<T>::clearKeyframes() {
    keyframes_.clear();
}

template <typename T>
T Animatable<T>::evaluate(FrameTime time) const {
    if (keyframes_.empty()) {
        return value_;
    }
    if (time <= keyframes_.front().time) {
        return keyframes_.front().value;
    }
    if (time >= keyframes_.back().time) {
        return keyframes_.back().value;
    }

    auto it = upperBound(time);
    const Keyframe<T>& from = *(it - 1);
    const Keyframe<T>& to = *it;

    const float progress = float(time - from.time) / float(to.time - from.time);
    const float easedProgress = applyEasing(from.easing, progress);

    if constexpr (std::is_same_v<T, Vec2>) {
        if (from.spatialOutTangent && to.spatialInTangent) {
            return evaluateSpatial(from, to, easedProgress);
        }
    }
    return Interpolator<T>::lerp(from.value, to.value, easedProgress);
}

template <typename T>
bool Animatable<T>::isAnimated() const {
    return !keyframes_.empty();
}

template <typename T>
const T& Animatable<T>::staticValue() const {
    return value_;
}

template <typename T>
void Animatable<T>::setStaticValue(T value) {
    value_ = std::move(value);
}

template <typename T>
const std::vector<Keyframe<T>>& Animatable<T>::keyframes() const {
    return keyframes_;
}

template <typename T>
typename std::vector<Keyframe<T>>::iterator Animatable<T>::lowerBound(FrameTime time) {
    return std::lower_bound(
        keyframes_.begin(), keyframes_.end(), time,
        [](const Keyframe<T>& keyframe, FrameTime target) {
            return keyframe.time < target;
        });
}

template <typename T>
typename std::vector<Keyframe<T>>::const_iterator Animatable<T>::upperBound(
    FrameTime time) const {
    return std::upper_bound(
        keyframes_.begin(), keyframes_.end(), time,
        [](FrameTime target, const Keyframe<T>& keyframe) {
            return target < keyframe.time;
        });
}

template <typename T>
typename std::vector<Keyframe<T>>::iterator Animatable<T>::find(FrameTime time) {
    auto it = lowerBound(time);
    return (it != keyframes_.end() && it->time == time) ? it : keyframes_.end();
}

// valueType() 按类型特化（须在显式实例化之前定义）。
template <>
AnimatableType Animatable<float>::valueType() const {
    return AnimatableType::Float;
}
template <>
AnimatableType Animatable<Vec2>::valueType() const {
    return AnimatableType::Vec2;
}
template <>
AnimatableType Animatable<Color>::valueType() const {
    return AnimatableType::Color;
}
template <>
AnimatableType Animatable<BezierPath>::valueType() const {
    return AnimatableType::BezierPath;
}
template <>
AnimatableType Animatable<std::string>::valueType() const {
    return AnimatableType::String;
}

// 支持的可动画类型登记处。
template class Animatable<float>;
template class Animatable<Vec2>;
template class Animatable<Color>;
template class Animatable<BezierPath>;
template class Animatable<std::string>;

}  // namespace motion
