#pragma once

#include "MotionStudio/common/Vec2.h"

namespace motion {

// 3x3 affine matrix (row-major, column-vector convention: p' = M * p).
// Transform chains compose via left-multiplication:
//   local = T(position) * R(rotation) * S(scale) * T(-anchor),
//   world = parentWorld * local.
struct Mat3 {
    float values[9] = {1, 0, 0,   //
                       0, 1, 0,   //
                       0, 0, 1};

    // Returns the 3x3 identity matrix.
    static Mat3 Identity();

    // Returns a translation matrix.
    // offset: translation vector.
    static Mat3 Translate(Vec2 offset);

    // Returns a rotation matrix.
    // degrees: rotation angle in degrees (counter-clockwise).
    static Mat3 Rotate(float degrees);

    // Returns a non-uniform scale matrix.
    // factor: scale factors along each axis.
    static Mat3 Scale(Vec2 factor);

    // Returns the matrix product (this * other).
    // other: right-hand operand.
    Mat3 operator*(const Mat3& other) const;

    // Transforms a point by this matrix: returns M * p.
    // point: the point to transform.
    Vec2 transformPoint(Vec2 point) const;

    bool operator==(const Mat3& other) const;
    bool operator!=(const Mat3& other) const;
};

}  // namespace motion
