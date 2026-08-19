#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/undo/GroupLayers.h"
#include "MotionStudio/undo/UndoManager.h"

using motion::Composition;
using motion::Document;
using motion::EntityId;
using motion::Layer;
using motion::LayerType;
using motion::MakeGroupLayersCommand;
using motion::Mat3;
using motion::UndoManager;
using motion::Vec2;

namespace {

int IndexOf(const Composition &composition, EntityId layerId) {
    for (int index = 0; index < static_cast<int>(composition.layers.size()); ++index) {
        if (composition.layers[static_cast<size_t>(index)]->id == layerId) {
            return index;
        }
    }
    return -1;
}

}  // namespace

TEST(GroupLayersTest, GroupsSiblingsUnderIdentityGroup) {
    Document document;
    UndoManager undo;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *a = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    Layer *b = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    a->transform.position.setStaticValue(Vec2{10, 0});
    b->transform.position.setStaticValue(Vec2{40, 0});
    const Mat3 worldA = a->worldTransform(0, document);
    const Mat3 worldB = b->worldTransform(0, document);

    EntityId groupId;
    std::unique_ptr<motion::Command> command =
        MakeGroupLayersCommand(document, composition->id, {a->id, b->id}, groupId);
    ASSERT_NE(command, nullptr);
    undo.execute(document, std::move(command));

    Layer *group = document.entityIndex().findLayer(groupId);
    ASSERT_NE(group, nullptr);
    EXPECT_EQ(group->type(), LayerType::Group);
    EXPECT_EQ(group->name, "Group");
    EXPECT_EQ(a->parentId, groupId);
    EXPECT_EQ(b->parentId, groupId);
    EXPECT_EQ(a->worldTransform(0, document), worldA);
    EXPECT_EQ(b->worldTransform(0, document), worldB);
    EXPECT_GT(IndexOf(*composition, groupId), IndexOf(*composition, a->id));
    EXPECT_GT(IndexOf(*composition, groupId), IndexOf(*composition, b->id));

    undo.undo(document);
    EXPECT_EQ(document.entityIndex().findLayer(groupId), nullptr);
    EXPECT_FALSE(a->parentId.isValid());
}

TEST(GroupLayersTest, SingleLayerAllowed) {
    Document document;
    UndoManager undo;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *a = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    EntityId groupId;
    std::unique_ptr<motion::Command> command =
        MakeGroupLayersCommand(document, composition->id, {a->id}, groupId);
    ASSERT_NE(command, nullptr);
    undo.execute(document, std::move(command));
    EXPECT_EQ(a->parentId, groupId);
}

TEST(GroupLayersTest, DifferentParentsIsNoop) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *g1 = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Group));
    Layer *g2 = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Group));
    Layer *a = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    Layer *b = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    ASSERT_TRUE(a->setParent(g1->id, document));
    ASSERT_TRUE(b->setParent(g2->id, document));
    EntityId groupId;
    EXPECT_EQ(MakeGroupLayersCommand(document, composition->id, {a->id, b->id}, groupId), nullptr);
}

TEST(GroupLayersTest, StripsDescendantsOfSelectedAncestors) {
    Document document;
    UndoManager undo;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *group = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Group));
    Layer *child = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    ASSERT_TRUE(child->setParent(group->id, document));
    EntityId outerId;
    std::unique_ptr<motion::Command> command =
        MakeGroupLayersCommand(document, composition->id, {group->id, child->id}, outerId);
    ASSERT_NE(command, nullptr);
    undo.execute(document, std::move(command));
    EXPECT_EQ(group->parentId, outerId);
    EXPECT_EQ(child->parentId, group->id);
}
