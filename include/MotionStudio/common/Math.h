#pragma once

#include <cmath>
#include <cstdint>

namespace motion {

struct Vec2 {
    float x = 0;
    float y = 0;

    Vec2 operator+(const Vec2& other) const { return {x + other.x, y + other.y}; }
    Vec2 operator-(const Vec2& other) const { return {x - other.x, y - other.y}; }
    Vec2 operator*(float scalar) const { return {x * scalar, y * scalar}; }
    Vec2 operator-() const { return {-x, -y}; }

    bool operator==(const Vec2& other) const { return x == other.x && y == other.y; }
    bool operator!=(const Vec2& other) const { return !(*this == other); }
};

// RGBA，各分量线性空间 [0, 1]。
struct Color {
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 1;

    bool operator==(const Color& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
    bool operator!=(const Color& other) const { return !(*this == other); }
};

// 3×3 仿射矩阵（行主序，列向量约定：p' = M · p）。
// 变换链左乘组合：local = T(position) · R(rotation) · S(scale) · T(-anchor)，
// world = parentWorld · local。
struct Mat3 {
    float values[9] = {1, 0, 0,   //
                       0, 1, 0,   //
                       0, 0, 1};

    static Mat3 identity() { return {}; }

    static Mat3 translate(Vec2 offset) {
        Mat3 result;
        result.values[2] = offset.x;
        result.values[5] = offset.y;
        return result;
    }

    static Mat3 rotate(float degrees) {
        const float radians = degrees * 3.14159265358979323846f / 180.0f;
        const float cosValue = std::cos(radians);
        const float sinValue = std::sin(radians);
        Mat3 result;
        result.values[0] = cosValue;
        result.values[1] = -sinValue;
        result.values[3] = sinValue;
        result.values[4] = cosValue;
        return result;
    }

    static Mat3 scale(Vec2 factor) {
        Mat3 result;
        result.values[0] = factor.x;
        result.values[4] = factor.y;
        return result;
    }

    Mat3 operator*(const Mat3& other) const {
        Mat3 result;
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                float sum = 0;
                for (int k = 0; k < 3; ++k) {
                    sum += values[row * 3 + k] * other.values[k * 3 + col];
                }
                result.values[row * 3 + col] = sum;
            }
        }
        return result;
    }

    Vec2 transformPoint(Vec2 point) const {
        return {values[0] * point.x + values[1] * point.y + values[2],
                values[3] * point.x + values[4] * point.y + values[5]};
    }

    bool operator==(const Mat3& other) const {
        for (int i = 0; i < 9; ++i) {
            if (values[i] != other.values[i]) return false;
        }
        return true;
    }
    bool operator!=(const Mat3& other) const { return !(*this == other); }
};

inline bool approxEqual(float left, float right, float epsilon = 1e-5f) {
    return std::abs(left - right) <= epsilon;
}

inline bool approxEqual(Vec2 left, Vec2 right, float epsilon = 1e-5f) {
    return approxEqual(left.x, right.x, epsilon) && approxEqual(left.y, right.y, epsilon);
}

}  // namespace motion
