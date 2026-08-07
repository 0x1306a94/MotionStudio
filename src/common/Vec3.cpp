#include "MotionStudio/common/Vec3.h"

#include "MotionStudio/common/Vec2.h"

namespace motion {

Vec3 Vec3::operator+(const Vec3 &other) const {
    return {x + other.x, y + other.y, z + other.z};
}

Vec3 Vec3::operator-(const Vec3 &other) const {
    return {x - other.x, y - other.y, z - other.z};
}

Vec3 Vec3::operator*(float scalar) const {
    return {x * scalar, y * scalar, z * scalar};
}

Vec3 Vec3::operator-() const {
    return {-x, -y, -z};
}

bool Vec3::operator==(const Vec3 &other) const {
    return x == other.x && y == other.y && z == other.z;
}

bool Vec3::operator!=(const Vec3 &other) const {
    return !(*this == other);
}

bool ApproxEqual(Vec3 left, Vec3 right, float epsilon) {
    return ApproxEqual(left.x, right.x, epsilon) && ApproxEqual(left.y, right.y, epsilon) &&
        ApproxEqual(left.z, right.z, epsilon);
}

}  // namespace motion
