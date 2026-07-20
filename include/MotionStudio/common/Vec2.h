#pragma once

namespace motion {

// 2D vector / point with basic arithmetic and comparison.
struct Vec2 {
    float x = 0;
    float y = 0;

    // Returns the component-wise sum.
    // other: vector to add.
    Vec2 operator+(const Vec2& other) const;

    // Returns the component-wise difference.
    // other: vector to subtract.
    Vec2 operator-(const Vec2& other) const;

    // Returns the vector scaled by scalar.
    // scalar: uniform scale factor.
    Vec2 operator*(float scalar) const;

    // Returns the negated vector.
    Vec2 operator-() const;

    bool operator==(const Vec2& other) const;
    bool operator!=(const Vec2& other) const;
};

// Returns true if left and right are within epsilon of each other.
// left: first value.
// right: second value.
// epsilon: tolerance threshold.
bool ApproxEqual(float left, float right, float epsilon = 1e-5f);

// Returns true if left and right are component-wise within epsilon.
// left: first vector.
// right: second vector.
// epsilon: tolerance threshold.
bool ApproxEqual(Vec2 left, Vec2 right, float epsilon = 1e-5f);

}  // namespace motion
