#pragma once

namespace motion {

// 缓动曲线：描述从当前关键帧到下一关键帧的插值节奏。
// Bezier 类型定义三次贝塞尔 P0=(0,0) P1=(inX,inY) P2=(outX,outY) P3=(1,1)，
// 参数语义与 CSS cubic-bezier(x1, y1, x2, y2) 一致：(inX,inY)=(x1,y1)，(outX,outY)=(x2,y2)。
struct Easing {
    enum class Type { Linear, Bezier, Hold };

    Type type = Type::Linear;
    float inX = 0;
    float inY = 0;
    float outX = 1;
    float outY = 1;

    static Easing Linear();
    static Easing Hold();
    static Easing Bezier(float inX, float inY, float outX, float outY);
    static Easing EaseIn();   // Bezier(0.42, 0, 1, 1)
    static Easing EaseOut();  // Bezier(0, 0, 0.58, 1)

    bool operator==(const Easing& other) const;
    bool operator!=(const Easing& other) const;
};

// 时间进度 progress ∈ [0,1] → 值进度 [0,1]（Bezier 时 y 可越界产生回弹效果）。
float applyEasing(const Easing& easing, float progress);

// 贝塞尔缓动求解：给定 x 求曲线上的 y（牛顿迭代 + 二分兜底，与 CSS 引擎同策略）。
// 要求 x1, x2 ∈ [0,1]（x 轴单调）；y1, y2 可越界。
float solveBezierEasing(float x1, float y1, float x2, float y2, float x);

}  // namespace motion
