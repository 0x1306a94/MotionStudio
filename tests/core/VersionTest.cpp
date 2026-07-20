#include <string>

#include <gtest/gtest.h>

#include "MotionStudio/core/Version.h"

TEST(VersionTest, ReturnsNonEmptyVersion) {
    const char *versionString = motion::Version();
    ASSERT_NE(versionString, nullptr);
    EXPECT_GT(std::string(versionString).size(), 0u);
}
