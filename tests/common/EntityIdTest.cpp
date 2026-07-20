#include <unordered_set>

#include <gtest/gtest.h>

#include "MotionStudio/common/EntityId.h"

using motion::EntityId;

TEST(EntityIdTest, DefaultIsInvalid) {
    EntityId id;
    EXPECT_EQ(id.value, 0u);
    EXPECT_FALSE(id.isValid());
}

TEST(EntityIdTest, GenerateReturnsValidUniqueIds) {
    std::unordered_set<EntityId> ids;
    for (int i = 0; i < 10000; ++i) {
        EntityId id = EntityId::generate();
        ASSERT_TRUE(id.isValid());
        ids.insert(id);
    }
    EXPECT_EQ(ids.size(), 10000u);
}
