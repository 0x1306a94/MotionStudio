#include "MotionStudio/common/Mat3.h"

#include <cmath>

namespace motion {

Mat3 Mat3::Identity() {
    return {};
}

Mat3 Mat3::Translate(Vec2 offset) {
    Mat3 result;
    result.values[2] = offset.x;
    result.values[5] = offset.y;
    return result;
}

Mat3 Mat3::Rotate(float degrees) {
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

Mat3 Mat3::Scale(Vec2 factor) {
    Mat3 result;
    result.values[0] = factor.x;
    result.values[4] = factor.y;
    return result;
}

Mat3 Mat3::operator*(const Mat3 &other) const {
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

Vec2 Mat3::transformPoint(Vec2 point) const {
    return {values[0] * point.x + values[1] * point.y + values[2],
            values[3] * point.x + values[4] * point.y + values[5]};
}

Vec2 Mat3::transformVector(Vec2 vector) const {
    return {values[0] * vector.x + values[1] * vector.y,
            values[3] * vector.x + values[4] * vector.y};
}

bool Mat3::tryInvert(Mat3 &out) const {
    const float a = values[0];
    const float b = values[1];
    const float c = values[3];
    const float d = values[4];
    const float tx = values[2];
    const float ty = values[5];
    const float det = a * d - b * c;
    if (std::fabs(det) <= 1e-8f) {
        return false;
    }
    const float invDet = 1.0f / det;
    out = Identity();
    out.values[0] = d * invDet;
    out.values[1] = -b * invDet;
    out.values[3] = -c * invDet;
    out.values[4] = a * invDet;
    out.values[2] = -(out.values[0] * tx + out.values[1] * ty);
    out.values[5] = -(out.values[3] * tx + out.values[4] * ty);
    return true;
}

bool Mat3::operator==(const Mat3 &other) const {
    for (int i = 0; i < 9; ++i) {
        if (values[i] != other.values[i]) {
            return false;
        }
    }
    return true;
}

bool Mat3::operator!=(const Mat3 &other) const {
    return !(*this == other);
}

}  // namespace motion
