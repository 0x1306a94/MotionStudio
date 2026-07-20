#pragma once

namespace motion {

struct Vec2 {
    float x = 0;
    float y = 0;

    Vec2 operator+(const Vec2& other) const;
    Vec2 operator-(const Vec2& other) const;
    Vec2 operator*(float scalar) const;
    Vec2 operator-() const;

    bool operator==(const Vec2& other) const;
    bool operator!=(const Vec2& other) const;
};

bool approxEqual(float left, float right, float epsilon = 1e-5f);
bool approxEqual(Vec2 left, Vec2 right, float epsilon = 1e-5f);

}  // namespace motion
