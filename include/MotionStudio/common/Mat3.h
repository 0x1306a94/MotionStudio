#pragma once

#include "MotionStudio/common/Vec2.h"

namespace motion {

// 3×3 仿射矩阵（行主序，列向量约定：p' = M · p）。
// 变换链左乘组合：local = T(position) · R(rotation) · S(scale) · T(-anchor)，
// world = parentWorld · local。
struct Mat3 {
    float values[9] = {1, 0, 0,   //
                       0, 1, 0,   //
                       0, 0, 1};

    static Mat3 identity();
    static Mat3 translate(Vec2 offset);
    static Mat3 rotate(float degrees);
    static Mat3 scale(Vec2 factor);

    Mat3 operator*(const Mat3& other) const;
    Vec2 transformPoint(Vec2 point) const;

    bool operator==(const Mat3& other) const;
    bool operator!=(const Mat3& other) const;
};

}  // namespace motion
