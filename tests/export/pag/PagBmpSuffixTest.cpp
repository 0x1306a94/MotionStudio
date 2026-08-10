#include <gtest/gtest.h>

#include "PagBmpSuffix.h"

using motion::pag_export::HasBmpSuffix;

TEST(PagBmpSuffixTest, DetectsSuffixCaseInsensitive) {
    EXPECT_TRUE(HasBmpSuffix("Comp_bmp"));
    EXPECT_TRUE(HasBmpSuffix("Layer_BMP"));
    EXPECT_TRUE(HasBmpSuffix("a_bmp"));
    EXPECT_FALSE(HasBmpSuffix("bmp_Comp"));
    EXPECT_FALSE(HasBmpSuffix("Comp"));
    EXPECT_FALSE(HasBmpSuffix(""));
    EXPECT_FALSE(HasBmpSuffix("bm"));
}

TEST(PagBmpSuffixTest, RequiresExactSuffix) {
    EXPECT_FALSE(HasBmpSuffix("Comp_bmp2"));
    EXPECT_FALSE(HasBmpSuffix("Compbmp"));
    EXPECT_TRUE(HasBmpSuffix("_bmp"));
}
