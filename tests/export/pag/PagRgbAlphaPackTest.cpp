#include "PagRgbAlphaPack.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using motion::pag_export::PackRgbAlphaSideBySide;

TEST(PagRgbAlphaPackTest, SideBySideAlphaAtRight) {
    // 2x1 RGBA: pixel0=(10,20,30,128), pixel1=(1,2,3,255)
    const int width = 2;
    const int height = 1;
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    const uint8_t rgba[] = {
        10,
        20,
        30,
        128,
        1,
        2,
        3,
        255,
    };

    std::vector<uint8_t> outPixels;
    int outWidth = 0;
    int outHeight = 0;
    ASSERT_TRUE(PackRgbAlphaSideBySide(rgba, width, height, rowBytes, &outPixels, &outWidth,
                                       &outHeight));
    EXPECT_GE(outWidth, 4);
    EXPECT_EQ(outWidth % 2, 0);
    EXPECT_GE(outHeight, 1);
    EXPECT_EQ(outHeight % 2, 0);
    EXPECT_EQ(static_cast<int>(outPixels.size()), outWidth * outHeight * 4);

    const size_t outRowBytes = static_cast<size_t>(outWidth) * 4;
    const uint8_t *left0 = outPixels.data();
    EXPECT_EQ(left0[0], 10);
    EXPECT_EQ(left0[1], 20);
    EXPECT_EQ(left0[2], 30);

    const uint8_t *right0 = outPixels.data() + static_cast<size_t>(width) * 4;
    EXPECT_EQ(right0[0], 128);
    EXPECT_EQ(right0[1], 128);
    EXPECT_EQ(right0[2], 128);
    EXPECT_EQ(right0[3], 255);

    const uint8_t *left1 = outPixels.data() + 4;
    EXPECT_EQ(left1[0], 1);
    EXPECT_EQ(left1[1], 2);
    EXPECT_EQ(left1[2], 3);

    const uint8_t *right1 = outPixels.data() + static_cast<size_t>(width) * 4 + 4;
    EXPECT_EQ(right1[0], 255);
    EXPECT_EQ(right1[1], 255);
    EXPECT_EQ(right1[2], 255);
    EXPECT_EQ(right1[3], 255);

    // Even-align padding rows/cols stay zero if present.
    if (outHeight > height) {
        for (size_t i = outRowBytes * static_cast<size_t>(height); i < outPixels.size(); ++i) {
            EXPECT_EQ(outPixels[i], 0);
        }
    }
}

TEST(PagRgbAlphaPackTest, RejectsInvalidInput) {
    std::vector<uint8_t> outPixels;
    int outWidth = 0;
    int outHeight = 0;
    const uint8_t rgba[] = {0, 0, 0, 0};
    EXPECT_FALSE(PackRgbAlphaSideBySide(nullptr, 1, 1, 4, &outPixels, &outWidth, &outHeight));
    EXPECT_FALSE(PackRgbAlphaSideBySide(rgba, 0, 1, 4, &outPixels, &outWidth, &outHeight));
    EXPECT_FALSE(PackRgbAlphaSideBySide(rgba, 1, 1, 4, nullptr, &outWidth, &outHeight));
}
