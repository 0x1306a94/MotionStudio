#include "MotionStudio/common/Vec4.h"

#include "MotionStudio/common/Vec2.h"

namespace motion {

Vec4 Vec4::operator+(const Vec4 &other) const {
    return {x + other.x, y + other.y, z + other.z, w + other.w};
}

Vec4 Vec4::operator-(const Vec4 &other) const {
    return {x - other.x, y - other.y, z - other.z, w - other.w};
}

Vec4 Vec4::operator*(float scalar) const {
    return {x * scalar, y * scalar, z * scalar, w * scalar};
}

Vec4 Vec4::operator-() const {
    return {-x, -y, -z, -w};
}

bool Vec4::operator==(const Vec4 &other) const {
    return x == other.x && y == other.y && z == other.z && w == other.w;
}

bool Vec4::operator!=(const Vec4 &other) const {
    return !(*this == other);
}

bool ApproxEqual(Vec4 left, Vec4 right, float epsilon) {
    return ApproxEqual(left.x, right.x, epsilon) && ApproxEqual(left.y, right.y, epsilon) &&
        ApproxEqual(left.z, right.z, epsilon) && ApproxEqual(left.w, right.w, epsilon);
}

}  // namespace motion
