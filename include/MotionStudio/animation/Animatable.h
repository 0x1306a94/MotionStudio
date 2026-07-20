#pragma once

#include <algorithm>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "MotionStudio/animation/Interpolator.h"
#include "MotionStudio/animation/Keyframe.h"
#include "MotionStudio/common/Time.h"

namespace motion {

// 非模板基类：供 PropertyPath 解析返回，命令再 dynamic_cast 回具体 Animatable<T>。
class AnimatableBase {
public:
    virtual ~AnimatableBase() = default;
};

// 可动画属性：要么静态值，要么关键帧序列。
// 关键帧操作只允许由 Command 调用，UI 不直接改模型。
template <typename T>
class Animatable : public AnimatableBase {
public:
    Animatable() = default;
    explicit Animatable(T staticValue) : value_(std::move(staticValue)) {}

    // 按 time 有序插入；同 time 已存在则替换。
    void addKeyframe(Keyframe<T> keyframe) {
        auto it = lowerBound(keyframe.time);
        if (it != keyframes_.end() && it->time == keyframe.time) {
            *it = std::move(keyframe);
        } else {
            keyframes_.insert(it, std::move(keyframe));
        }
    }

    void removeKeyframe(FrameTime time) {
        auto it = find(time);
        if (it != keyframes_.end()) keyframes_.erase(it);
    }

    // 移除并返回关键帧（MoveKeyframe 用）；不存在返回 nullopt。
    std::optional<Keyframe<T>> takeKeyframe(FrameTime time) {
        auto it = find(time);
        if (it == keyframes_.end()) return std::nullopt;
        Keyframe<T> taken = std::move(*it);
        keyframes_.erase(it);
        return taken;
    }

    // 更新 time 处的关键帧；不存在返回 false。
    bool updateKeyframe(FrameTime time, Keyframe<T> keyframe) {
        auto it = find(time);
        if (it == keyframes_.end()) return false;
        keyframe.time = time;
        *it = std::move(keyframe);
        return true;
    }

    void clearKeyframes() { keyframes_.clear(); }

    // 求值：无关键帧返回静态值；区间外钳制到首/末帧值（不外推）。
    T evaluate(FrameTime time) const {
        if (keyframes_.empty()) return value_;
        if (time <= keyframes_.front().time) return keyframes_.front().value;
        if (time >= keyframes_.back().time) return keyframes_.back().value;

        auto it = upperBound(time);
        const Keyframe<T>& from = *(it - 1);
        const Keyframe<T>& to = *it;

        const float progress =
            float(time - from.time) / float(to.time - from.time);
        const float easedProgress = applyEasing(from.easing, progress);

        if constexpr (std::is_same_v<T, Vec2>) {
            if (from.spatialOutTangent && to.spatialInTangent) {
                return evaluateSpatial(from, to, easedProgress);
            }
        }
        return Interpolator<T>::lerp(from.value, to.value, easedProgress);
    }

    bool isAnimated() const { return !keyframes_.empty(); }

    const T& staticValue() const { return value_; }
    void setStaticValue(T value) { value_ = std::move(value); }

    const std::vector<Keyframe<T>>& keyframes() const { return keyframes_; }

private:
    auto lowerBound(FrameTime time) {
        return std::lower_bound(
            keyframes_.begin(), keyframes_.end(), time,
            [](const Keyframe<T>& keyframe, FrameTime target) {
                return keyframe.time < target;
            });
    }

    auto upperBound(FrameTime time) const {
        return std::upper_bound(
            keyframes_.begin(), keyframes_.end(), time,
            [](FrameTime target, const Keyframe<T>& keyframe) {
                return target < keyframe.time;
            });
    }

    auto find(FrameTime time) {
        auto it = lowerBound(time);
        return (it != keyframes_.end() && it->time == time) ? it : keyframes_.end();
    }

    T value_{};
    std::vector<Keyframe<T>> keyframes_;  // 按 time 升序
};

}  // namespace motion
