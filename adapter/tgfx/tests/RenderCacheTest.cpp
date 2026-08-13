#include <gtest/gtest.h>
#include <memory>

#include "RenderCache.h"

using motion::FilterResources;
using motion::RenderCache;

TEST(RenderCacheTest, StoresAndClearsFilterResources) {
    RenderCache cache;
    EXPECT_EQ(cache.findFilterResources(1), nullptr);

    auto resources = std::make_unique<FilterResources>();
    FilterResources *raw = resources.get();
    cache.addFilterResources(1, std::move(resources));
    EXPECT_EQ(cache.findFilterResources(1), raw);
    EXPECT_EQ(cache.findFilterResources(2), nullptr);

    cache.addFilterResources(1, nullptr);
    EXPECT_EQ(cache.findFilterResources(1), raw);

    cache.releaseAll();
    EXPECT_EQ(cache.findFilterResources(1), nullptr);
}
