#include <gtest/gtest.h>

#include "OnScreenTransform.h"

using motion::MakeOnScreenTransform;
using motion::ScreenTransform;

namespace {

constexpr float EPSILON = 1e-5f;

}  // namespace

TEST(OnScreenTransformTest, ScalesDownToFitCentered) {
    // 1920x1080 scene into an 800x800 drawable: fit scale 800/1920, dest rect
    // 800x450 centered vertically (letterboxed).
    const ScreenTransform transform = MakeOnScreenTransform(1920, 1080, 800, 800, 1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(transform.scaleX, 800.0f / 1920.0f, EPSILON);
    EXPECT_NEAR(transform.scaleY, 450.0f / 1080.0f, EPSILON);
    EXPECT_NEAR(transform.skewX, 0.0f, EPSILON);
    EXPECT_NEAR(transform.skewY, 0.0f, EPSILON);
    EXPECT_NEAR(transform.translateX, 0.0f, EPSILON);
    EXPECT_NEAR(transform.translateY, 175.0f, EPSILON);
}

TEST(OnScreenTransformTest, NeverScalesAboveOneToOne) {
    // 100x100 scene into a 400x400 drawable: rendered 1:1 and centered.
    const ScreenTransform transform = MakeOnScreenTransform(100, 100, 400, 400, 1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(transform.scaleX, 1.0f, EPSILON);
    EXPECT_NEAR(transform.scaleY, 1.0f, EPSILON);
    EXPECT_NEAR(transform.translateX, 150.0f, EPSILON);
    EXPECT_NEAR(transform.translateY, 150.0f, EPSILON);
}

TEST(OnScreenTransformTest, ZoomMagnifiesAboutDrawableOrigin) {
    const float zoom = 2.0f;
    const ScreenTransform fitted = MakeOnScreenTransform(1920, 1080, 800, 800, 1.0f, 0.0f, 0.0f, 1.0f);
    const ScreenTransform zoomed = MakeOnScreenTransform(1920, 1080, 800, 800, zoom, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(zoomed.scaleX, zoom * fitted.scaleX, EPSILON);
    EXPECT_NEAR(zoomed.scaleY, zoom * fitted.scaleY, EPSILON);
    EXPECT_NEAR(zoomed.translateX, zoom * fitted.translateX, EPSILON);
    EXPECT_NEAR(zoomed.translateY, zoom * fitted.translateY, EPSILON);
}

TEST(OnScreenTransformTest, PanConvertsPointsToDrawablePixels) {
    const float contentsScale = 2.0f;
    const ScreenTransform fitted = MakeOnScreenTransform(1920, 1080, 800, 800, 1.0f, 0.0f, 0.0f, contentsScale);
    const ScreenTransform panned = MakeOnScreenTransform(1920, 1080, 800, 800, 1.0f, 10.0f, -5.0f, contentsScale);
    EXPECT_NEAR(panned.scaleX, fitted.scaleX, EPSILON);
    EXPECT_NEAR(panned.scaleY, fitted.scaleY, EPSILON);
    EXPECT_NEAR(panned.translateX, fitted.translateX + 10.0f * contentsScale, EPSILON);
    EXPECT_NEAR(panned.translateY, fitted.translateY - 5.0f * contentsScale, EPSILON);
}

TEST(OnScreenTransformTest, NonPositiveZoomFallsBackToFit) {
    const ScreenTransform fitted = MakeOnScreenTransform(1920, 1080, 800, 800, 1.0f, 3.0f, 4.0f, 1.0f);
    const ScreenTransform invalid = MakeOnScreenTransform(1920, 1080, 800, 800, 0.0f, 3.0f, 4.0f, 1.0f);
    EXPECT_NEAR(invalid.scaleX, fitted.scaleX, EPSILON);
    EXPECT_NEAR(invalid.scaleY, fitted.scaleY, EPSILON);
    EXPECT_NEAR(invalid.translateX, fitted.translateX, EPSILON);
    EXPECT_NEAR(invalid.translateY, fitted.translateY, EPSILON);
}

TEST(OnScreenTransformTest, InvalidSizesYieldIdentity) {
    const ScreenTransform transform = MakeOnScreenTransform(0, 1080, 800, 800, 2.0f, 3.0f, 4.0f, 1.0f);
    EXPECT_NEAR(transform.scaleX, 1.0f, EPSILON);
    EXPECT_NEAR(transform.scaleY, 1.0f, EPSILON);
    EXPECT_NEAR(transform.translateX, 0.0f, EPSILON);
    EXPECT_NEAR(transform.translateY, 0.0f, EPSILON);
}
