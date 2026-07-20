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

namespace {

// Comparison functions for std::lower_bound / std::upper_bound (avoids lambdas).
template <typename T>
bool KeyframeBeforeTime(const Keyframe<T> &keyframe, FrameTime time) {
    return keyframe.time < time;
}

template <typename T>
bool TimeBeforeKeyframe(FrameTime time, const Keyframe<T> &keyframe) {
    return time < keyframe.time;
}

}  // namespace

template <typename T>
Animatable<T>::Animatable(T staticValue)
    : value_(std::move(staticValue)) {
}

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
    const Keyframe<T> &from = *(it - 1);
    const Keyframe<T> &to = *it;

    const float progress = float(time - from.time) / float(to.time - from.time);
    const float easedProgress = ApplyEasing(from.easing, progress);

    if constexpr (std::is_same_v<T, Vec2>) {
        if (from.spatialOutTangent && to.spatialInTangent) {
            return EvaluateSpatial(from, to, easedProgress);
        }
    }
    return Interpolator<T>::Lerp(from.value, to.value, easedProgress);
}

template <typename T>
bool Animatable<T>::isAnimated() const {
    return !keyframes_.empty();
}

template <typename T>
const T &Animatable<T>::staticValue() const {
    return value_;
}

template <typename T>
void Animatable<T>::setStaticValue(T value) {
    value_ = std::move(value);
}

template <typename T>
const std::vector<Keyframe<T>> &Animatable<T>::keyframes() const {
    return keyframes_;
}

template <typename T>
typename std::vector<Keyframe<T>>::iterator Animatable<T>::lowerBound(FrameTime time) {
    return std::lower_bound(keyframes_.begin(), keyframes_.end(), time,
                            KeyframeBeforeTime<T>);
}

template <typename T>
typename std::vector<Keyframe<T>>::const_iterator Animatable<T>::upperBound(
    FrameTime time) const {
    return std::upper_bound(keyframes_.begin(), keyframes_.end(), time,
                            TimeBeforeKeyframe<T>);
}

template <typename T>
typename std::vector<Keyframe<T>>::iterator Animatable<T>::find(FrameTime time) {
    auto it = lowerBound(time);
    return (it != keyframes_.end() && it->time == time) ? it : keyframes_.end();
}

// valueType() specializations (must be defined before explicit instantiations).
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

// Explicit instantiations for supported animatable types.
template class Animatable<float>;
template class Animatable<Vec2>;
template class Animatable<Color>;
template class Animatable<BezierPath>;
template class Animatable<std::string>;

}  // namespace motion
