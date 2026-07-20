#pragma once

#include <optional>
#include <vector>

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
// 实现与显式实例化见 src/animation/Animatable.cpp；
// 新增可动画类型需在那里登记实例化。
template <typename T>
class Animatable : public AnimatableBase {
public:
    Animatable() = default;
    // 非 explicit：支持 `Animatable<float> opacity{1};` 式的成员初始化。
    Animatable(T staticValue);

    // 按 time 有序插入；同 time 已存在则替换。
    void addKeyframe(Keyframe<T> keyframe);
    void removeKeyframe(FrameTime time);
    // 移除并返回关键帧（MoveKeyframe 用）；不存在返回 nullopt。
    std::optional<Keyframe<T>> takeKeyframe(FrameTime time);
    // 更新 time 处的关键帧；不存在返回 false。
    bool updateKeyframe(FrameTime time, Keyframe<T> keyframe);
    void clearKeyframes();

    // 求值：无关键帧返回静态值；区间外钳制到首/末帧值（不外推）。
    T evaluate(FrameTime time) const;

    bool isAnimated() const;

    const T& staticValue() const;
    void setStaticValue(T value);

    const std::vector<Keyframe<T>>& keyframes() const;

private:
    typename std::vector<Keyframe<T>>::iterator lowerBound(FrameTime time);
    typename std::vector<Keyframe<T>>::const_iterator upperBound(FrameTime time) const;
    typename std::vector<Keyframe<T>>::iterator find(FrameTime time);

    T value_{};
    std::vector<Keyframe<T>> keyframes_;  // 按 time 升序
};

}  // namespace motion
