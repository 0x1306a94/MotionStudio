#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/animation/Keyframe.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/undo/GroupLayers.h"
#include "MotionStudio/undo/UndoManager.h"

using motion::Composition;
using motion::Document;
using motion::EntityId;
using motion::Keyframe;
using motion::Layer;
using motion::LayerType;
using motion::MakeGroupLayersCommand;
using motion::MakeRemoveLayerCommand;
using motion::MakeUngroupLayersCommand;
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

TEST(GroupLayersTest, KeepsExistingSubtreeContiguous) {
    Document document;
    UndoManager undo;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *base = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    Layer *faceShape = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    Layer *label = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Text));
    Layer *face = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Group));
    ASSERT_TRUE(faceShape->setParent(face->id, document));
    ASSERT_TRUE(label->setParent(face->id, document));

    EntityId groupId;
    std::unique_ptr<motion::Command> command =
        MakeGroupLayersCommand(document, composition->id, {face->id, base->id}, groupId);
    ASSERT_NE(command, nullptr);
    undo.execute(document, std::move(command));

    EXPECT_EQ(face->parentId, groupId);
    EXPECT_EQ(base->parentId, groupId);
    EXPECT_EQ(faceShape->parentId, face->id);
    EXPECT_EQ(label->parentId, face->id);
    EXPECT_EQ(IndexOf(*composition, base->id), 0);
    EXPECT_EQ(IndexOf(*composition, faceShape->id), 1);
    EXPECT_EQ(IndexOf(*composition, label->id), 2);
    EXPECT_EQ(IndexOf(*composition, face->id), 3);
    EXPECT_EQ(IndexOf(*composition, groupId), 4);

    undo.undo(document);
    EXPECT_EQ(document.entityIndex().findLayer(groupId), nullptr);
    EXPECT_EQ(IndexOf(*composition, base->id), 0);
    EXPECT_EQ(IndexOf(*composition, faceShape->id), 1);
    EXPECT_EQ(IndexOf(*composition, label->id), 2);
    EXPECT_EQ(IndexOf(*composition, face->id), 3);
}

TEST(GroupLayersTest, UngroupIdentityRestoresParentAndWorld) {
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
    std::unique_ptr<motion::Command> groupCommand =
        MakeGroupLayersCommand(document, composition->id, {a->id, b->id}, groupId);
    ASSERT_NE(groupCommand, nullptr);
    undo.execute(document, std::move(groupCommand));

    std::unique_ptr<motion::Command> ungroupCommand =
        MakeUngroupLayersCommand(document, composition->id, {groupId}, 0);
    ASSERT_NE(ungroupCommand, nullptr);
    undo.execute(document, std::move(ungroupCommand));

    EXPECT_EQ(document.entityIndex().findLayer(groupId), nullptr);
    EXPECT_FALSE(a->parentId.isValid());
    EXPECT_FALSE(b->parentId.isValid());
    EXPECT_EQ(a->worldTransform(0, document), worldA);
    EXPECT_EQ(b->worldTransform(0, document), worldB);
}

TEST(GroupLayersTest, UngroupBakesStaticTransform) {
    Document document;
    UndoManager undo;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *a = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    a->transform.position.setStaticValue(Vec2{10, 0});

    EntityId groupId;
    std::unique_ptr<motion::Command> groupCommand =
        MakeGroupLayersCommand(document, composition->id, {a->id}, groupId);
    ASSERT_NE(groupCommand, nullptr);
    undo.execute(document, std::move(groupCommand));
    Layer *group = document.entityIndex().findLayer(groupId);
    ASSERT_NE(group, nullptr);
    group->transform.position.setStaticValue(Vec2{100, 0});

    std::unique_ptr<motion::Command> ungroupCommand =
        MakeUngroupLayersCommand(document, composition->id, {groupId}, 0);
    ASSERT_NE(ungroupCommand, nullptr);
    undo.execute(document, std::move(ungroupCommand));

    EXPECT_EQ(document.entityIndex().findLayer(groupId), nullptr);
    EXPECT_FALSE(a->parentId.isValid());
    EXPECT_EQ(a->worldTransform(0, document), Mat3::Translate(Vec2{110, 0}));
    EXPECT_FALSE(a->transform.position.isAnimated());
}

TEST(GroupLayersTest, UngroupSkipsKeyframedChildPosition) {
    Document document;
    UndoManager undo;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *a = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    Keyframe<Vec2> keyframe;
    keyframe.time = 0;
    keyframe.value = Vec2{10, 0};
    a->transform.position.addKeyframe(keyframe);

    EntityId groupId;
    std::unique_ptr<motion::Command> groupCommand =
        MakeGroupLayersCommand(document, composition->id, {a->id}, groupId);
    ASSERT_NE(groupCommand, nullptr);
    undo.execute(document, std::move(groupCommand));
    Layer *group = document.entityIndex().findLayer(groupId);
    ASSERT_NE(group, nullptr);
    group->transform.position.setStaticValue(Vec2{100, 0});

    std::unique_ptr<motion::Command> ungroupCommand =
        MakeUngroupLayersCommand(document, composition->id, {groupId}, 0);
    ASSERT_NE(ungroupCommand, nullptr);
    undo.execute(document, std::move(ungroupCommand));

    ASSERT_EQ(a->transform.position.keyframes().size(), 1u);
    EXPECT_EQ(a->transform.position.keyframes()[0].value, (Vec2{10, 0}));
}

TEST(GroupLayersTest, UngroupIgnoresNonGroupSelection) {
    Document document;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *shape = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    EXPECT_EQ(MakeUngroupLayersCommand(document, composition->id, {shape->id}, 0), nullptr);
}

TEST(GroupLayersTest, RemovingGroupDeletesDescendants) {
    Document document;
    UndoManager undo;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *group = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Group));
    Layer *child = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    Layer *nested = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Group));
    Layer *grandchild = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    Layer *sibling = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    ASSERT_TRUE(child->setParent(group->id, document));
    ASSERT_TRUE(nested->setParent(group->id, document));
    ASSERT_TRUE(grandchild->setParent(nested->id, document));
    const EntityId groupId = group->id;
    const EntityId childId = child->id;
    const EntityId nestedId = nested->id;
    const EntityId grandchildId = grandchild->id;
    const EntityId siblingId = sibling->id;

    undo.execute(document, MakeRemoveLayerCommand(document, composition->id, groupId));

    EXPECT_EQ(document.entityIndex().findLayer(groupId), nullptr);
    EXPECT_EQ(document.entityIndex().findLayer(childId), nullptr);
    EXPECT_EQ(document.entityIndex().findLayer(nestedId), nullptr);
    EXPECT_EQ(document.entityIndex().findLayer(grandchildId), nullptr);
    EXPECT_NE(document.entityIndex().findLayer(siblingId), nullptr);
    EXPECT_EQ(composition->layers.size(), 1u);

    undo.undo(document);
    EXPECT_NE(document.entityIndex().findLayer(groupId), nullptr);
    EXPECT_NE(document.entityIndex().findLayer(childId), nullptr);
    EXPECT_NE(document.entityIndex().findLayer(nestedId), nullptr);
    EXPECT_NE(document.entityIndex().findLayer(grandchildId), nullptr);
    EXPECT_EQ(document.entityIndex().findLayer(childId)->parentId, groupId);
    EXPECT_EQ(document.entityIndex().findLayer(nestedId)->parentId, groupId);
    EXPECT_EQ(document.entityIndex().findLayer(grandchildId)->parentId, nestedId);
    EXPECT_EQ(composition->layers.size(), 5u);

    undo.redo(document);
    EXPECT_EQ(document.entityIndex().findLayer(groupId), nullptr);
    EXPECT_EQ(document.entityIndex().findLayer(childId), nullptr);
    EXPECT_NE(document.entityIndex().findLayer(siblingId), nullptr);
}

TEST(GroupLayersTest, RemovingShapeLeavesSiblings) {
    Document document;
    UndoManager undo;
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *group = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Group));
    Layer *child = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    ASSERT_TRUE(child->setParent(group->id, document));
    const EntityId childId = child->id;
    const EntityId groupId = group->id;

    undo.execute(document, MakeRemoveLayerCommand(document, composition->id, childId));

    EXPECT_EQ(document.entityIndex().findLayer(childId), nullptr);
    EXPECT_NE(document.entityIndex().findLayer(groupId), nullptr);
    EXPECT_EQ(composition->layers.size(), 1u);
}
