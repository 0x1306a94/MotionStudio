#include "MotionStudio/animation/Easing.h"

#include <cmath>

namespace motion {

namespace {

constexpr float kEasingEqualityEpsilon = 1e-5f;

bool AlmostEqual(float left, float right) {
    return std::abs(left - right) <= kEasingEqualityEpsilon;
}

}  // namespace

Easing Easing::Linear() {
    return {};
}

Easing Easing::Hold() {
    Easing easing;
    easing.type = EasingType::Hold;
    return easing;
}

Easing Easing::Bezier(float inX, float inY, float outX, float outY) {
    Easing easing;
    easing.type = EasingType::CubicBezier;
    easing.inX = inX;
    easing.inY = inY;
    easing.outX = outX;
    easing.outY = outY;
    return easing;
}

Easing Easing::Ease() {
    Easing easing = Bezier(0.25f, 0.1f, 0.25f, 1);
    easing.type = EasingType::Ease;
    return easing;
}

Easing Easing::EaseIn() {
    Easing easing = Bezier(0.42f, 0, 1, 1);
    easing.type = EasingType::EaseIn;
    return easing;
}

Easing Easing::EaseOut() {
    Easing easing = Bezier(0, 0, 0.58f, 1);
    easing.type = EasingType::EaseOut;
    return easing;
}

Easing Easing::EaseInOut() {
    Easing easing = Bezier(0.42f, 0, 0.58f, 1);
    easing.type = EasingType::EaseInOut;
    return easing;
}

bool Easing::operator==(const Easing &other) const {
    if (type != other.type) {
        return false;
    }
    if (type != EasingType::CubicBezier) {
        return true;
    }
    return AlmostEqual(inX, other.inX) && AlmostEqual(inY, other.inY) &&
        AlmostEqual(outX, other.outX) && AlmostEqual(outY, other.outY);
}

bool Easing::operator!=(const Easing &other) const {
    return !(*this == other);
}

namespace {

// B(t) = 3(1-t)²t·p1 + 3(1-t)t²·p2 + t³ (endpoints fixed at 0 and 1).
float CubicBezierAxis(float p1, float p2, float t) {
    const float mt = 1 - t;
    return 3 * mt * mt * t * p1 + 3 * mt * t * t * p2 + t * t * t;
}

// B'(t) = 3(1-t)²·p1 + 6(1-t)t·(p2-p1) + 3t²·(1-p2).
float CubicBezierAxisDerivative(float p1, float p2, float t) {
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

    // Phase 1: Newton-Raphson (typically converges in 4-8 iterations).
    float t = x;
    bool converged = false;
    for (int i = 0; i < 8; ++i) {
        const float error = CubicBezierAxis(x1, x2, t) - x;
        if (std::abs(error) < 1e-7f) {
            converged = true;
            break;
        }
        const float derivative = CubicBezierAxisDerivative(x1, x2, t);
        if (std::abs(derivative) < 1e-7f) {
            break;  // near-flat tangent, Newton makes no progress
        }
        t -= error / derivative;
        if (t < 0 || t > 1) {
            break;  // strayed outside the curve domain
        }
    }

    // Phase 2: bisection whenever Newton did not converge (near-vertical
    // handles or straying), matching Blink/WebKit behavior.
    if (!converged) {
        float lo = 0;
        float hi = 1;
        t = x;
        for (int i = 0; i < 32; ++i) {
            const float current = CubicBezierAxis(x1, x2, t);
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

    return CubicBezierAxis(y1, y2, t);
}

float ApplyEasing(const Easing &easing, float progress) {
    switch (easing.type) {
        case EasingType::Linear: {
            return progress;
        }
        case EasingType::Hold: {
            return 0;
        }
        case EasingType::Ease:
        case EasingType::EaseIn:
        case EasingType::EaseOut:
        case EasingType::EaseInOut:
        case EasingType::CubicBezier: {
            return SolveBezierEasing(easing.inX, easing.inY, easing.outX, easing.outY, progress);
        }
    }
    return progress;
}

}  // namespace motion
