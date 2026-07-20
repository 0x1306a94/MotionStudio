#pragma once

#include "MotionStudio/animation/Keyframe.h"
#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Math.h"

namespace motion {

// 插值策略 trait：Animatable<T> 不关心 T 的细节，插值方式经此注入。
template <typename T>
struct Interpolator;

template <>
struct Interpolator<float> {
    static float lerp(float from, float to, float t) { return from + (to - from) * t; }
};

template <>
struct Interpolator<Vec2> {
    static Vec2 lerp(const Vec2& from, const Vec2& to, float t) {
        return from + (to - from) * t;
    }
};

template <>
struct Interpolator<Color> {
    static Color lerp(const Color& from, const Color& to, float t);
};

template <>
struct Interpolator<BezierPath> {
    // 逐顶点插值。M1 要求两路径顶点数一致，不一致抛 std::invalid_argument
    //（自动顶点插入匹配留到 M2）。
    static BezierPath lerp(const BezierPath& from, const BezierPath& to, float t);
};

// 三次贝塞尔曲线取点：B(t) = (1-t)³P0 + 3(1-t)²t·P1 + 3(1-t)t²·P2 + t³·P3。
Vec2 cubicBezierPoint(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float t);

// 空间插值：有空间手柄时沿贝塞尔弧线运动，否则退化为直线插值。
Vec2 evaluateSpatial(const Keyframe<Vec2>& from, const Keyframe<Vec2>& to,
                     float easedProgress);

}  // namespace motion
