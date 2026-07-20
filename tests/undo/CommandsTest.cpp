#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeFill.h"
#include "MotionStudio/model/TextContent.h"
#include "MotionStudio/undo/AddKeyframeCommand.h"
#include "MotionStudio/undo/AddLayerCommand.h"
#include "MotionStudio/undo/MoveKeyframeCommand.h"
#include "MotionStudio/undo/MoveLayerCommand.h"
#include "MotionStudio/undo/RemoveKeyframeCommand.h"
#include "MotionStudio/undo/RemoveLayerCommand.h"
#include "MotionStudio/undo/SetEasingCommand.h"
#include "MotionStudio/undo/SetStaticValueCommand.h"
#include "MotionStudio/undo/UndoManager.h"

using motion::AddKeyframeCommand;
using motion::AddLayerCommand;
using motion::Animatable;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::Easing;
using motion::EntityId;
using motion::Keyframe;
using motion::KeyframeData;
using motion::Layer;
using motion::LayerType;
using motion::MoveKeyframeCommand;
using motion::MoveLayerCommand;
using motion::PropertyPath;
using motion::PropertyValue;
using motion::RemoveKeyframeCommand;
using motion::RemoveLayerCommand;
using motion::SetEasingCommand;
using motion::SetStaticValueCommand;
using motion::ShapeContent;
using motion::ShapeFill;
using motion::UndoManager;
using motion::Vec2;

namespace {

struct Scene {
    Document document;
    UndoManager undo;
    Composition *composition;
    Layer *layer;

    Scene() {
        composition = document.addComposition(std::make_unique<Composition>());
        layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    }

    template <typename CommandType, typename... Args>
    void execute(Args &&...args) {
        undo.execute(document,
                     std::make_unique<CommandType>(std::forward<Args>(args)...));
    }
};

PropertyPath TransformPosition(EntityId layerId) {
    return {layerId, "transform.position"};
}

Keyframe<Vec2> PositionKeyframe(motion::FrameTime time, Vec2 value,
                                Easing easing = Easing::Linear()) {
    Keyframe<Vec2> keyframe;
    keyframe.time = time;
    keyframe.value = value;
    keyframe.easing = easing;
    return keyframe;
}

}  // namespace

TEST(AddLayerCommandTest, AddUndoRedo) {
    Scene scene;
    auto layer = std::make_unique<Layer>(LayerType::Null);
    const EntityId layerId = layer->id;

    scene.execute<AddLayerCommand>(scene.composition->id, std::move(layer));
    ASSERT_EQ(scene.composition->layers.size(), 2u);
    EXPECT_NE(scene.document.entityIndex().findLayer(layerId), nullptr);

    scene.undo.undo(scene.document);
    EXPECT_EQ(scene.composition->layers.size(), 1u);
    EXPECT_EQ(scene.document.entityIndex().findLayer(layerId), nullptr);

    scene.undo.redo(scene.document);
    EXPECT_EQ(scene.composition->layers.size(), 2u);
    EXPECT_EQ(scene.composition->layers[1]->id, layerId);
}

TEST(AddLayerCommandTest, InsertsAtIndex) {
    Scene scene;
    auto layer = std::make_unique<Layer>(LayerType::Null);
    const EntityId layerId = layer->id;

    scene.execute<AddLayerCommand>(scene.composition->id, std::move(layer), 0);
    EXPECT_EQ(scene.composition->layers[0]->id, layerId);
}

TEST(RemoveLayerCommandTest, UndoRestoresSubtreeWithKeyframes) {
    Scene scene;
    auto *shapeContent = static_cast<ShapeContent *>(scene.layer->content.get());
    auto fill = std::make_unique<ShapeFill>();
    auto *fillRaw = fill.get();
    shapeContent->elements.push_back(std::move(fill));
    scene.document.refreshEntityIndex();

    Keyframe<Color> colorKeyframe;
    colorKeyframe.time = 10;
    colorKeyframe.value = Color{1, 0, 0, 1};
    fillRaw->color.addKeyframe(colorKeyframe);
    const EntityId layerId = scene.layer->id;

    scene.execute<RemoveLayerCommand>(scene.composition->id, layerId);
    EXPECT_TRUE(scene.composition->layers.empty());
    EXPECT_EQ(scene.document.entityIndex().findShape(fillRaw->id), nullptr);

    scene.undo.undo(scene.document);
    ASSERT_EQ(scene.composition->layers.size(), 1u);
    auto *restoredFill = scene.document.entityIndex().findShape(fillRaw->id);
    ASSERT_NE(restoredFill, nullptr);
    EXPECT_EQ(restoredFill->id, fillRaw->id);
    auto *restoredLayer = scene.document.entityIndex().findLayer(layerId);
    auto *restoredContent = static_cast<ShapeContent *>(restoredLayer->content.get());
    auto *fill2 = static_cast<ShapeFill *>(restoredContent->elements[0].get());
    EXPECT_EQ(fill2->color.keyframes().size(), 1u);
    EXPECT_EQ(fill2->color.evaluate(10), (Color{1, 0, 0, 1}));
}

TEST(RemoveLayerCommandTest, ExecuteSkipsMissingLayer) {
    Scene scene;
    scene.execute<RemoveLayerCommand>(scene.composition->id, EntityId{999});
    // Target missing → no-op; empty command still pushed, undo is safe.
    scene.undo.undo(scene.document);
    EXPECT_EQ(scene.composition->layers.size(), 1u);
}

TEST(MoveLayerCommandTest, MoveAndUndo) {
    Scene scene;
    Layer *second =
        scene.document.addLayer(scene.composition->id, std::make_unique<Layer>(LayerType::Null));
    Layer *third =
        scene.document.addLayer(scene.composition->id, std::make_unique<Layer>(LayerType::Null));

    scene.execute<MoveLayerCommand>(scene.composition->id, 0, 2);
    EXPECT_EQ(scene.composition->layers[2]->id, scene.layer->id);

    scene.undo.undo(scene.document);
    EXPECT_EQ(scene.composition->layers[0]->id, scene.layer->id);
    (void)second;
    (void)third;
}

TEST(MoveLayerCommandTest, ConsecutiveDragsMerge) {
    Scene scene;
    for (int i = 0; i < 3; ++i) {
        scene.document.addLayer(scene.composition->id, std::make_unique<Layer>(LayerType::Null));
    }
    // Consecutive drags 0 → 1 → 2 → 3 merge into a single undo unit.
    scene.execute<MoveLayerCommand>(scene.composition->id, 0, 1);
    scene.execute<MoveLayerCommand>(scene.composition->id, 1, 2);
    scene.execute<MoveLayerCommand>(scene.composition->id, 2, 3);
    EXPECT_EQ(scene.composition->layers[3]->id, scene.layer->id);

    scene.undo.undo(scene.document);
    EXPECT_EQ(scene.composition->layers[0]->id, scene.layer->id);
    EXPECT_FALSE(scene.undo.canUndo());
}

TEST(SetStaticValueCommandTest, SetAndUndo) {
    Scene scene;
    PropertyPath path = TransformPosition(scene.layer->id);
    EXPECT_EQ(scene.layer->transform.position.staticValue(), (Vec2{0, 0}));

    scene.execute<SetStaticValueCommand>(path, PropertyValue{Vec2{100, 200}});
    EXPECT_EQ(scene.layer->transform.position.staticValue(), (Vec2{100, 200}));
    EXPECT_EQ(scene.layer->transform.position.evaluate(0), (Vec2{100, 200}));

    scene.undo.undo(scene.document);
    EXPECT_EQ(scene.layer->transform.position.staticValue(), (Vec2{0, 0}));
}

TEST(SetStaticValueCommandTest, MergesSameTargetKeepsOriginalValue) {
    Scene scene;
    PropertyPath path = TransformPosition(scene.layer->id);

    scene.execute<SetStaticValueCommand>(path, PropertyValue{Vec2{10, 0}});
    scene.execute<SetStaticValueCommand>(path, PropertyValue{Vec2{20, 0}});
    scene.execute<SetStaticValueCommand>(path, PropertyValue{Vec2{30, 0}});
    EXPECT_EQ(scene.layer->transform.position.staticValue(), (Vec2{30, 0}));

    scene.undo.undo(scene.document);
    EXPECT_EQ(scene.layer->transform.position.staticValue(), (Vec2{0, 0}));
}

TEST(SetStaticValueCommandTest, TypeMismatchIsNoOp) {
    Scene scene;
    PropertyPath path = TransformPosition(scene.layer->id);

    scene.execute<SetStaticValueCommand>(path, PropertyValue{1.0f});
    EXPECT_EQ(scene.layer->transform.position.staticValue(), (Vec2{0, 0}));
}

TEST(AddKeyframeCommandTest, AddAndUndo) {
    Scene scene;
    PropertyPath path = TransformPosition(scene.layer->id);

    scene.execute<AddKeyframeCommand>(path, KeyframeData{PositionKeyframe(10, {50, 0})});
    ASSERT_TRUE(scene.layer->transform.position.isAnimated());
    EXPECT_EQ(scene.layer->transform.position.evaluate(10), (Vec2{50, 0}));

    scene.undo.undo(scene.document);
    EXPECT_FALSE(scene.layer->transform.position.isAnimated());
}

TEST(AddKeyframeCommandTest, UndoRestoresReplacedKeyframe) {
    Scene scene;
    PropertyPath path = TransformPosition(scene.layer->id);
    scene.layer->transform.position.addKeyframe(PositionKeyframe(10, {1, 1}));

    scene.execute<AddKeyframeCommand>(path, KeyframeData{PositionKeyframe(10, {9, 9})});
    EXPECT_EQ(scene.layer->transform.position.evaluate(10), (Vec2{9, 9}));

    scene.undo.undo(scene.document);
    ASSERT_EQ(scene.layer->transform.position.keyframes().size(), 1u);
    EXPECT_EQ(scene.layer->transform.position.evaluate(10), (Vec2{1, 1}));
}

TEST(RemoveKeyframeCommandTest, RemoveAndUndoRestoresExactly) {
    Scene scene;
    PropertyPath path = TransformPosition(scene.layer->id);
    scene.layer->transform.position.addKeyframe(
        PositionKeyframe(10, {50, 0}, Easing::EaseOut()));

    scene.execute<RemoveKeyframeCommand>(path, 10);
    EXPECT_FALSE(scene.layer->transform.position.isAnimated());

    scene.undo.undo(scene.document);
    ASSERT_EQ(scene.layer->transform.position.keyframes().size(), 1u);
    const Keyframe<Vec2> &restored = scene.layer->transform.position.keyframes()[0];
    EXPECT_EQ(restored.time, motion::FrameTime(10));
    EXPECT_EQ(restored.value, (Vec2{50, 0}));
    EXPECT_EQ(restored.easing, Easing::EaseOut());
}

TEST(RemoveKeyframeCommandTest, MissingFrameIsNoOp) {
    Scene scene;
    PropertyPath path = TransformPosition(scene.layer->id);

    scene.execute<RemoveKeyframeCommand>(path, 42);
    scene.undo.undo(scene.document);
    EXPECT_FALSE(scene.layer->transform.position.isAnimated());
}

TEST(MoveKeyframeCommandTest, MoveAndUndo) {
    Scene scene;
    PropertyPath path = TransformPosition(scene.layer->id);
    scene.layer->transform.position.addKeyframe(PositionKeyframe(10, {50, 0}));

    scene.execute<MoveKeyframeCommand>(path, 10, 30);
    ASSERT_EQ(scene.layer->transform.position.keyframes().size(), 1u);
    EXPECT_EQ(scene.layer->transform.position.keyframes()[0].time, motion::FrameTime(30));

    scene.undo.undo(scene.document);
    ASSERT_EQ(scene.layer->transform.position.keyframes().size(), 1u);
    EXPECT_EQ(scene.layer->transform.position.keyframes()[0].time, motion::FrameTime(10));
    EXPECT_EQ(scene.layer->transform.position.keyframes()[0].value, (Vec2{50, 0}));
}

TEST(MoveKeyframeCommandTest, UndoRestoresOverwrittenKeyframe) {
    Scene scene;
    PropertyPath path = TransformPosition(scene.layer->id);
    auto &position = scene.layer->transform.position;
    position.addKeyframe(PositionKeyframe(10, {10, 0}));
    position.addKeyframe(PositionKeyframe(30, {30, 0}));

    scene.execute<MoveKeyframeCommand>(path, 10, 30);
    ASSERT_EQ(position.keyframes().size(), 1u);
    EXPECT_EQ(position.keyframes()[0].value, (Vec2{10, 0}));

    scene.undo.undo(scene.document);
    ASSERT_EQ(position.keyframes().size(), 2u);
    EXPECT_EQ(position.keyframes()[0].value, (Vec2{10, 0}));
    EXPECT_EQ(position.keyframes()[1].value, (Vec2{30, 0}));
}

TEST(MoveKeyframeCommandTest, ConsecutiveDragsMerge) {
    Scene scene;
    PropertyPath path = TransformPosition(scene.layer->id);
    scene.layer->transform.position.addKeyframe(PositionKeyframe(10, {10, 0}));

    scene.execute<MoveKeyframeCommand>(path, 10, 11);
    scene.execute<MoveKeyframeCommand>(path, 11, 12);
    scene.execute<MoveKeyframeCommand>(path, 12, 13);
    ASSERT_EQ(scene.layer->transform.position.keyframes().size(), 1u);
    EXPECT_EQ(scene.layer->transform.position.keyframes()[0].time, motion::FrameTime(13));

    scene.undo.undo(scene.document);
    EXPECT_EQ(scene.layer->transform.position.keyframes()[0].time, motion::FrameTime(10));
}

TEST(SetEasingCommandTest, SetAndUndo) {
    Scene scene;
    PropertyPath path = TransformPosition(scene.layer->id);
    scene.layer->transform.position.addKeyframe(PositionKeyframe(10, {50, 0}));

    scene.execute<SetEasingCommand>(path, 10, Easing::EaseIn());
    EXPECT_EQ(scene.layer->transform.position.keyframes()[0].easing, Easing::EaseIn());

    scene.undo.undo(scene.document);
    EXPECT_EQ(scene.layer->transform.position.keyframes()[0].easing, Easing::Linear());
}

TEST(SetEasingCommandTest, MissingFrameIsNoOp) {
    Scene scene;
    PropertyPath path = TransformPosition(scene.layer->id);

    scene.execute<SetEasingCommand>(path, 99, Easing::EaseIn());
    scene.undo.undo(scene.document);
    EXPECT_FALSE(scene.layer->transform.position.isAnimated());
}

TEST(CommandsTest, SkipWhenTargetEntityDeleted) {
    Scene scene;
    PropertyPath path = TransformPosition(scene.layer->id);

    scene.execute<AddKeyframeCommand>(path, KeyframeData{PositionKeyframe(10, {50, 0})});
    scene.execute<RemoveLayerCommand>(scene.composition->id, scene.layer->id);

    // Layer deleted: new command silently skips, no crash.
    scene.execute<SetStaticValueCommand>(path, PropertyValue{Vec2{1, 1}});
    scene.undo.undo(scene.document);  // Skipped command undo is also a no-op

    scene.undo.undo(scene.document);
    ASSERT_EQ(scene.composition->layers.size(), 1u);
    scene.undo.undo(scene.document);  // Remove keyframe (layer restored, takes effect)
    EXPECT_FALSE(scene.layer->transform.position.isAnimated());
}
