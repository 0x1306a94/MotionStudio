// Easing solver acceptance: compare SolveBezierEasing against a
// high-precision CSS cubic-bezier() reference (pure bisection), error < 1e-5.
#include <cmath>

#include <gtest/gtest.h>

#include "MotionStudio/animation/Easing.h"

using motion::Easing;
using motion::SolveBezierEasing;

namespace {

// B(t) = 3(1-t)^2 t p1 + 3(1-t) t^2 p2 + t^3, endpoints fixed at 0 and 1.
float ReferenceAxis(float p1, float p2, float t) {
    const float mt = 1.0f - t;
    return 3.0f * mt * mt * t * p1 + 3.0f * mt * t * t * p2 + t * t * t;
}

// CSS reference: 100-iteration bisection on x (about 2^-100 precision).
float CssCubicBezier(float x1, float y1, float x2, float y2, float x) {
    if (x <= 0.0f) {
        return 0.0f;
    }
    if (x >= 1.0f) {
        return 1.0f;
    }
    float lo = 0.0f;
    float hi = 1.0f;
    float t = x;
    for (int i = 0; i < 100; ++i) {
        const float current = ReferenceAxis(x1, x2, t);
        if (std::abs(current - x) < 1e-9f) {
            break;
        }
        if (current < x) {
            lo = t;
        } else {
            hi = t;
        }
        t = (lo + hi) * 0.5f;
    }
    return ReferenceAxis(y1, y2, t);
}

constexpr float kTolerance = 1e-5f;

void CompareAgainstCss(float x1, float y1, float x2, float y2) {
    for (int i = 1; i < 100; ++i) {
        const float x = float(i) / 100.0f;
        EXPECT_NEAR(SolveBezierEasing(x1, y1, x2, y2, x),
                    CssCubicBezier(x1, y1, x2, y2, x), kTolerance)
            << "x=" << x << " curve=(" << x1 << "," << y1 << "," << x2 << "," << y2
            << ")";
    }
}

}  // namespace

TEST(EasingCssTest, PresetsMatchCssDefinitions) {
    // CSS ease-in = cubic-bezier(0.42, 0, 1, 1), ease-out = cubic-bezier(0, 0, 0.58, 1).
    const Easing easeIn = Easing::EaseIn();
    CompareAgainstCss(easeIn.inX, easeIn.inY, easeIn.outX, easeIn.outY);
    const Easing easeOut = Easing::EaseOut();
    CompareAgainstCss(easeOut.inX, easeOut.inY, easeOut.outX, easeOut.outY);
}

TEST(EasingCssTest, StandardCssCurves) {
    CompareAgainstCss(0.25f, 0.1f, 0.25f, 1.0f);  // ease
    CompareAgainstCss(0.42f, 0.0f, 0.58f, 1.0f);  // ease-in-out
    CompareAgainstCss(0.0f, 0.0f, 1.0f, 1.0f);    // linear
}

TEST(EasingCssTest, ExtremeYHandlesOvershoot) {
    CompareAgainstCss(0.5f, -0.5f, 0.5f, 1.5f);
    CompareAgainstCss(0.68f, -0.55f, 0.265f, 1.55f);  // classic "back" curve
}

TEST(EasingCssTest, NearVerticalHandles) {
    // Newton iteration struggles near vertical tangents; bisection fallback kicks in.
    CompareAgainstCss(0.99f, 0.0f, 1.0f, 1.0f);
    CompareAgainstCss(0.0f, 0.0f, 0.01f, 1.0f);
}

TEST(EasingCssTest, FineGridOnEaseIn) {
    const Easing easeIn = Easing::EaseIn();
    for (int i = 1; i < 1000; ++i) {
        const float x = float(i) / 1000.0f;
        EXPECT_NEAR(SolveBezierEasing(easeIn.inX, easeIn.inY, easeIn.outX, easeIn.outY, x),
                    CssCubicBezier(easeIn.inX, easeIn.inY, easeIn.outX, easeIn.outY, x),
                    kTolerance)
            << "x=" << x;
    }
}
