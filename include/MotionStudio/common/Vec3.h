#pragma once

namespace motion {

// 3D vector with basic arithmetic and comparison.
struct Vec3 {
    float x = 0;
    float y = 0;
    float z = 0;

    // Returns the component-wise sum.
    // other: vector to add.
    Vec3 operator+(const Vec3 &other) const;

    // Returns the component-wise difference.
    // other: vector to subtract.
    Vec3 operator-(const Vec3 &other) const;

    // Returns the vector scaled by scalar.
    // scalar: uniform scale factor.
    Vec3 operator*(float scalar) const;

    // Returns the negated vector.
    Vec3 operator-() const;

    bool operator==(const Vec3 &other) const;
    bool operator!=(const Vec3 &other) const;
};

// Returns true if left and right are component-wise within epsilon.
// left: first vector.
// right: second vector.
// epsilon: tolerance threshold.
bool ApproxEqual(Vec3 left, Vec3 right, float epsilon = 1e-5f);

}  // namespace motion
