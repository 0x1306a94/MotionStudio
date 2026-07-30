#include <gtest/gtest.h>

#include "MotionStudio/render/ImageScaleLayout.h"

using motion::ComputeImageDestinationRect;
using motion::ImageScaleMode;
using motion::Vec2;

TEST(ImageScaleLayoutTest, LetterBoxCentersInside) {
    const auto rect =
        ComputeImageDestinationRect({200, 100}, {100, 100}, ImageScaleMode::LetterBox);
    EXPECT_FLOAT_EQ(rect.width, 100.f);
    EXPECT_FLOAT_EQ(rect.height, 100.f);
    EXPECT_FLOAT_EQ(rect.x, 50.f);
    EXPECT_FLOAT_EQ(rect.y, 0.f);
}

TEST(ImageScaleLayoutTest, StretchFills) {
    const auto rect =
        ComputeImageDestinationRect({200, 100}, {50, 50}, ImageScaleMode::Stretch);
    EXPECT_FLOAT_EQ(rect.x, 0.f);
    EXPECT_FLOAT_EQ(rect.y, 0.f);
    EXPECT_FLOAT_EQ(rect.width, 200.f);
    EXPECT_FLOAT_EQ(rect.height, 100.f);
}

TEST(ImageScaleLayoutTest, ZoomCoversAndCrops) {
    const auto rect = ComputeImageDestinationRect({200, 100}, {100, 100}, ImageScaleMode::Zoom);
    EXPECT_FLOAT_EQ(rect.width, 200.f);
    EXPECT_FLOAT_EQ(rect.height, 200.f);
    EXPECT_FLOAT_EQ(rect.x, 0.f);
    EXPECT_FLOAT_EQ(rect.y, -50.f);
}

TEST(ImageScaleLayoutTest, NoneUsesIntrinsicAtOrigin) {
    const auto rect = ComputeImageDestinationRect({200, 100}, {80, 60}, ImageScaleMode::None);
    EXPECT_FLOAT_EQ(rect.x, 0.f);
    EXPECT_FLOAT_EQ(rect.y, 0.f);
    EXPECT_FLOAT_EQ(rect.width, 80.f);
    EXPECT_FLOAT_EQ(rect.height, 60.f);
}

TEST(ImageScaleLayoutTest, NonPositiveReturnsEmpty) {
    EXPECT_TRUE(ComputeImageDestinationRect({0, 100}, {50, 50}, ImageScaleMode::Stretch).isEmpty());
    EXPECT_TRUE(ComputeImageDestinationRect({100, 100}, {-1, 50}, ImageScaleMode::LetterBox)
                    .isEmpty());
}
