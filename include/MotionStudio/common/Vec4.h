#pragma once

namespace motion {

// 4D vector with basic arithmetic and comparison.
struct Vec4 {
    float x = 0;
    float y = 0;
    float z = 0;
    float w = 0;

    // Returns the component-wise sum.
    // other: vector to add.
    Vec4 operator+(const Vec4 &other) const;

    // Returns the component-wise difference.
    // other: vector to subtract.
    Vec4 operator-(const Vec4 &other) const;

    // Returns the vector scaled by scalar.
    // scalar: uniform scale factor.
    Vec4 operator*(float scalar) const;

    // Returns the negated vector.
    Vec4 operator-() const;

    bool operator==(const Vec4 &other) const;
    bool operator!=(const Vec4 &other) const;
};

// Returns true if left and right are component-wise within epsilon.
// left: first vector.
// right: second vector.
// epsilon: tolerance threshold.
bool ApproxEqual(Vec4 left, Vec4 right, float epsilon = 1e-5f);

}  // namespace motion
