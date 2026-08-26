#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerOrder.h"

using motion::Composition;
using motion::Document;
using motion::EntityId;
using motion::IsSubtreeContiguousOrder;
using motion::Layer;
using motion::LayerMoveSteps;
using motion::LayerType;
using motion::NormalizeSubtreeContiguousOrder;

namespace {

std::vector<EntityId> CurrentOrder(const Composition &composition) {
    std::vector<EntityId> ids;
    for (const auto &layer : composition.layers) {
        ids.push_back(layer->id);
    }
    return ids;
}

Layer *AddShape(Document &document, Composition &composition) {
    return document.addLayer(composition.id, std::make_unique<Layer>(LayerType::Shape));
}

Layer *AddGroup(Document &document, Composition &composition) {
    return document.addLayer(composition.id, std::make_unique<Layer>(LayerType::Group));
}

}  // namespace

TEST(LayerOrderTest, EmptyAndSingleLayerPassThrough) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    EXPECT_TRUE(NormalizeSubtreeContiguousOrder({}, *composition).empty());

    Layer *only = AddShape(document, *composition);
    const std::vector<EntityId> result = NormalizeSubtreeContiguousOrder({only->id}, *composition);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], only->id);
}

TEST(LayerOrderTest, FlatListWithoutParentsIsUnchanged) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *a = AddShape(document, *composition);
    Layer *b = AddShape(document, *composition);
    Layer *c = AddShape(document, *composition);

    const std::vector<EntityId> desired = {c->id, a->id, b->id};
    EXPECT_EQ(NormalizeSubtreeContiguousOrder(desired, *composition), desired);
}

TEST(LayerOrderTest, PullsDescendantsBelowTheirParent) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *base = AddShape(document, *composition);
    Layer *faceShape = AddShape(document, *composition);
    Layer *label = AddShape(document, *composition);
    Layer *face = AddGroup(document, *composition);
    Layer *group = AddGroup(document, *composition);
    ASSERT_TRUE(faceShape->setParent(face->id, document));
    ASSERT_TRUE(label->setParent(face->id, document));
    ASSERT_TRUE(base->setParent(group->id, document));
    ASSERT_TRUE(face->setParent(group->id, document));

    const std::vector<EntityId> desired = {faceShape->id, label->id, base->id, face->id, group->id};
    const std::vector<EntityId> expected = {base->id, faceShape->id, label->id, face->id, group->id};
    EXPECT_EQ(NormalizeSubtreeContiguousOrder(desired, *composition), expected);
}

TEST(LayerOrderTest, KeepsSiblingRelativeOrderFromDesired) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *first = AddShape(document, *composition);
    Layer *second = AddShape(document, *composition);
    Layer *parent = AddGroup(document, *composition);
    ASSERT_TRUE(first->setParent(parent->id, document));
    ASSERT_TRUE(second->setParent(parent->id, document));

    const std::vector<EntityId> forward = {first->id, second->id, parent->id};
    EXPECT_EQ(NormalizeSubtreeContiguousOrder(forward, *composition), forward);

    const std::vector<EntityId> reversed = {second->id, first->id, parent->id};
    EXPECT_EQ(NormalizeSubtreeContiguousOrder(reversed, *composition), reversed);
}

TEST(LayerOrderTest, NestedGroupsStayContiguous) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *outer = AddGroup(document, *composition);
    Layer *inner = AddGroup(document, *composition);
    Layer *leaf = AddShape(document, *composition);
    Layer *sibling = AddShape(document, *composition);
    ASSERT_TRUE(inner->setParent(outer->id, document));
    ASSERT_TRUE(leaf->setParent(inner->id, document));

    const std::vector<EntityId> desired = {outer->id, sibling->id, inner->id, leaf->id};
    const std::vector<EntityId> expected = {leaf->id, inner->id, outer->id, sibling->id};
    EXPECT_EQ(NormalizeSubtreeContiguousOrder(desired, *composition), expected);
}

TEST(LayerOrderTest, ParentOverridesWinOverStoredParentId) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *child = AddShape(document, *composition);
    Layer *group = AddGroup(document, *composition);
    Layer *other = AddShape(document, *composition);

    std::unordered_map<EntityId, EntityId> overrides;
    overrides[child->id] = group->id;
    const std::vector<EntityId> desired = {child->id, other->id, group->id};
    const std::vector<EntityId> expected = {other->id, child->id, group->id};
    EXPECT_EQ(NormalizeSubtreeContiguousOrder(desired, *composition, overrides), expected);
}

TEST(LayerOrderTest, AppendsMissingLayersAndDropsUnknownIds) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *a = AddShape(document, *composition);
    Layer *b = AddShape(document, *composition);
    Layer *c = AddShape(document, *composition);

    const std::vector<EntityId> desired = {c->id, EntityId::Generate(), a->id};
    const std::vector<EntityId> expected = {c->id, a->id, b->id};
    EXPECT_EQ(NormalizeSubtreeContiguousOrder(desired, *composition), expected);
}

TEST(LayerOrderTest, CyclicParentIdDoesNotHang) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *a = AddShape(document, *composition);
    Layer *b = AddShape(document, *composition);
    // setParent rejects cycles, so write the fields directly to build the broken state.
    a->parentId = b->id;
    b->parentId = a->id;

    const std::vector<EntityId> result = NormalizeSubtreeContiguousOrder({a->id, b->id}, *composition);
    EXPECT_EQ(result.size(), 2u);
}

TEST(LayerOrderTest, DetectsContiguousAndNonContiguousOrders) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *child = AddShape(document, *composition);
    Layer *stranger = AddShape(document, *composition);
    Layer *parent = AddGroup(document, *composition);
    ASSERT_TRUE(child->setParent(parent->id, document));

    EXPECT_FALSE(IsSubtreeContiguousOrder(*composition));

    const std::vector<EntityId> fixed = NormalizeSubtreeContiguousOrder(CurrentOrder(*composition), *composition);
    for (const std::pair<int, int> &step : LayerMoveSteps(CurrentOrder(*composition), fixed)) {
        document.moveLayer(composition->id, step.first, step.second);
    }
    EXPECT_TRUE(IsSubtreeContiguousOrder(*composition));
    EXPECT_EQ(CurrentOrder(*composition).front(), stranger->id);
    EXPECT_EQ(CurrentOrder(*composition).back(), parent->id);
}

TEST(LayerOrderTest, MoveStepsTransformFromIntoTo) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *a = AddShape(document, *composition);
    Layer *b = AddShape(document, *composition);
    Layer *c = AddShape(document, *composition);

    const std::vector<EntityId> from = {a->id, b->id, c->id};
    const std::vector<EntityId> to = {c->id, a->id, b->id};
    std::vector<EntityId> order = from;
    for (const std::pair<int, int> &step : LayerMoveSteps(from, to)) {
        const EntityId moved = order[static_cast<size_t>(step.first)];
        order.erase(order.begin() + step.first);
        order.insert(order.begin() + step.second, moved);
    }
    EXPECT_EQ(order, to);
}

TEST(LayerOrderTest, MoveStepsRejectsNonPermutations) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *a = AddShape(document, *composition);
    Layer *b = AddShape(document, *composition);

    EXPECT_TRUE(LayerMoveSteps({a->id, b->id}, {a->id}).empty());
    EXPECT_TRUE(LayerMoveSteps({a->id, b->id}, {a->id, EntityId::Generate()}).empty());
}
