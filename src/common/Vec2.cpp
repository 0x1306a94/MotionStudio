#include "MotionStudio/common/Vec2.h"

#include <cmath>

namespace motion {

Vec2 Vec2::operator+(const Vec2 &other) const {
    return {x + other.x, y + other.y};
}

Vec2 Vec2::operator-(const Vec2 &other) const {
    return {x - other.x, y - other.y};
}

Vec2 Vec2::operator*(float scalar) const {
    return {x * scalar, y * scalar};
}

Vec2 Vec2::operator-() const {
    return {-x, -y};
}

bool Vec2::operator==(const Vec2 &other) const {
    return x == other.x && y == other.y;
}

bool Vec2::operator!=(const Vec2 &other) const {
    return !(*this == other);
}

bool ApproxEqual(float left, float right, float epsilon) {
    return std::abs(left - right) <= epsilon;
}

bool ApproxEqual(Vec2 left, Vec2 right, float epsilon) {
    return ApproxEqual(left.x, right.x, epsilon) && ApproxEqual(left.y, right.y, epsilon);
}

}  // namespace motion
