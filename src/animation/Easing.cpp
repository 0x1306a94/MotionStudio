#include "MotionStudio/animation/Easing.h"

#include <cmath>

namespace motion {

Easing Easing::Linear() { return {}; }

Easing Easing::Hold() {
    Easing easing;
    easing.type = Type::Hold;
    return easing;
}

Easing Easing::Bezier(float inX, float inY, float outX, float outY) {
    Easing easing;
    easing.type = Type::Bezier;
    easing.inX = inX;
    easing.inY = inY;
    easing.outX = outX;
    easing.outY = outY;
    return easing;
}

Easing Easing::EaseIn() { return Bezier(0.42f, 0, 1, 1); }

Easing Easing::EaseOut() { return Bezier(0, 0, 0.58f, 1); }

bool Easing::operator==(const Easing& other) const {
    if (type != other.type) {
        return false;
    }
    if (type != Type::Bezier) {
        return true;
    }
    return inX == other.inX && inY == other.inY && outX == other.outX && outY == other.outY;
}

bool Easing::operator!=(const Easing& other) const { return !(*this == other); }

namespace {

// B(t) = 3(1-t)²t·p1 + 3(1-t)t²·p2 + t³（端点固定为 0 和 1）。
float cubicBezierAxis(float p1, float p2, float t) {
    const float mt = 1 - t;
    return 3 * mt * mt * t * p1 + 3 * mt * t * t * p2 + t * t * t;
}

// B'(t) = 3(1-t)²·p1 + 6(1-t)t·(p2-p1) + 3t²·(1-p2)。
float cubicBezierAxisDerivative(float p1, float p2, float t) {
    const float mt = 1 - t;
    return 3 * mt * mt * p1 + 6 * mt * t * (p2 - p1) + 3 * t * t * (1 - p2);
}

}  // namespace

float SolveBezierEasing(float x1, float y1, float x2, float y2, float x) {
    if (x <= 0) {
        return 0;
    }
    if (x >= 1) {
        return 1;
    }

    // 阶段一：牛顿迭代（通常 4~8 次收敛）。
    float t = x;
    for (int i = 0; i < 8; ++i) {
        const float error = cubicBezierAxis(x1, x2, t) - x;
        if (std::abs(error) < 1e-7f) {
            break;
        }
        const float derivative = cubicBezierAxisDerivative(x1, x2, t);
        if (std::abs(derivative) < 1e-7f) {
            break;
        }
        t -= error / derivative;
    }

    // 阶段二：牛顿发散时二分兜底。
    if (t < 0 || t > 1) {
        float lo = 0;
        float hi = 1;
        t = x;
        for (int i = 0; i < 20; ++i) {
            const float current = cubicBezierAxis(x1, x2, t);
            if (std::abs(current - x) < 1e-7f) {
                break;
            }
            if (current < x) {
                lo = t;
            } else {
                hi = t;
            }
            t = (lo + hi) * 0.5f;
        }
    }

    return cubicBezierAxis(y1, y2, t);
}

float ApplyEasing(const Easing& easing, float progress) {
    switch (easing.type) {
        case Easing::Type::Linear:
            return progress;
        case Easing::Type::Hold:
            return 0;
        case Easing::Type::Bezier:
            return SolveBezierEasing(easing.inX, easing.inY, easing.outX, easing.outY,
                                     progress);
    }
    return progress;
}

}  // namespace motion
