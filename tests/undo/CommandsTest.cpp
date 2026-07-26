#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/MaskMode.h"
#include "MotionStudio/model/TextContent.h"
#include "MotionStudio/model/TrackMatteType.h"
#include "MotionStudio/undo/AddKeyframeCommand.h"
#include "MotionStudio/undo/AddLayerCommand.h"
#include "MotionStudio/undo/AddLayerStyleCommand.h"
#include "MotionStudio/undo/AddMaskCommand.h"
#include "MotionStudio/undo/MoveKeyframeCommand.h"
#include "MotionStudio/undo/MoveLayerCommand.h"
#include "MotionStudio/undo/MoveMaskCommand.h"
#include "MotionStudio/undo/RemoveKeyframeCommand.h"
#include "MotionStudio/undo/RemoveLayerCommand.h"
#include "MotionStudio/undo/RemoveMaskCommand.h"
#include "MotionStudio/undo/RemoveStyleCommand.h"
#include "MotionStudio/undo/SetEasingCommand.h"
#include "MotionStudio/undo/SetMaskInvertedCommand.h"
#include "MotionStudio/undo/SetMaskModeCommand.h"
#include "MotionStudio/undo/SetStaticValueCommand.h"
#include "MotionStudio/undo/SetStrokePositionCommand.h"
#include "MotionStudio/undo/SetStyleBlendModeCommand.h"
#include "MotionStudio/undo/SetTrackMatteCommand.h"
#include "MotionStudio/undo/UndoManager.h"

using motion::AddKeyframeCommand;
using motion::AddLayerCommand;
using motion::AddLayerStyleCommand;
using motion::AddMaskCommand;
using motion::Animatable;
using motion::BezierPath;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::Easing;
using motion::EntityId;
using motion::FillStyle;
using motion::Keyframe;
using motion::KeyframeData;
using motion::Layer;
using motion::LayerType;
using motion::Mask;
using motion::MaskMode;
using motion::MoveKeyframeCommand;
using motion::MoveLayerCommand;
using motion::MoveMaskCommand;
using motion::PropertyPath;
using motion::PropertyValue;
using motion::RemoveKeyframeCommand;
using motion::RemoveLayerCommand;
using motion::RemoveMaskCommand;
using motion::RemoveStyleCommand;
using motion::SetEasingCommand;
using motion::SetMaskInvertedCommand;
using motion::SetMaskModeCommand;
using motion::SetStaticValueCommand;
using motion::SetStrokePositionCommand;
using motion::SetStyleBlendModeCommand;
using motion::SetTrackMatteCommand;
using motion::TrackMatteType;
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
    auto layer = std::make_unique<Layer>(LayerType::Group);
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
    auto layer = std::make_unique<Layer>(LayerType::Group);
    const EntityId layerId = layer->id;

    scene.execute<AddLayerCommand>(scene.composition->id, std::move(layer), 0);
    EXPECT_EQ(scene.composition->layers[0]->id, layerId);
}

TEST(RemoveLayerCommandTest, UndoRestoresSubtreeWithKeyframes) {
    Scene scene;
    auto fill = std::make_unique<FillStyle>();
    auto *fillRaw = fill.get();
    scene.layer->styles.push_back(std::move(fill));

    Keyframe<Color> colorKeyframe;
    colorKeyframe.time = 10;
    colorKeyframe.value = Color{1, 0, 0, 1};
    fillRaw->color.addKeyframe(colorKeyframe);
    const EntityId layerId = scene.layer->id;

    scene.execute<RemoveLayerCommand>(scene.composition->id, layerId);
    EXPECT_TRUE(scene.composition->layers.empty());

    scene.undo.undo(scene.document);
    ASSERT_EQ(scene.composition->layers.size(), 1u);
    auto *restoredLayer = scene.document.entityIndex().findLayer(layerId);
    ASSERT_NE(restoredLayer, nullptr);
    ASSERT_EQ(restoredLayer->styles.size(), 1u);
    auto *fill2 = static_cast<FillStyle *>(restoredLayer->styles[0].get());
    EXPECT_EQ(fill2->id, fillRaw->id);
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
        scene.document.addLayer(scene.composition->id, std::make_unique<Layer>(LayerType::Group));
    Layer *third =
        scene.document.addLayer(scene.composition->id, std::make_unique<Layer>(LayerType::Group));

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
        scene.document.addLayer(scene.composition->id, std::make_unique<Layer>(LayerType::Group));
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

TEST(AddLayerStyleCommandTest, AddFillUndoRedo) {
    Scene scene;

    scene.execute<AddLayerStyleCommand>(scene.layer->id, std::make_unique<FillStyle>());
    ASSERT_EQ(scene.layer->styles.size(), 1u);
    EXPECT_EQ(scene.layer->styles[0]->type(), motion::LayerStyleType::Fill);
    const EntityId styleId = scene.layer->styles[0]->id;

    scene.undo.undo(scene.document);
    EXPECT_TRUE(scene.layer->styles.empty());

    scene.undo.redo(scene.document);
    ASSERT_EQ(scene.layer->styles.size(), 1u);
    EXPECT_EQ(scene.layer->styles[0]->id, styleId);
}

TEST(AddLayerStyleCommandTest, ExecuteSkipsMissingLayer) {
    Scene scene;
    scene.execute<AddLayerStyleCommand>(EntityId{999}, std::make_unique<FillStyle>());
    scene.undo.undo(scene.document);
    EXPECT_TRUE(scene.layer->styles.empty());
}

TEST(RemoveStyleCommandTest, RemoveUndoRedo) {
    Scene scene;
    auto first = std::make_unique<FillStyle>();
    auto second = std::make_unique<FillStyle>();
    const EntityId firstId = first->id;
    const EntityId secondId = second->id;
    scene.layer->styles.push_back(std::move(first));
    scene.layer->styles.push_back(std::move(second));

    scene.execute<RemoveStyleCommand>(scene.layer->id, 0);
    ASSERT_EQ(scene.layer->styles.size(), 1u);
    EXPECT_EQ(scene.layer->styles[0]->id, secondId);

    scene.undo.undo(scene.document);
    ASSERT_EQ(scene.layer->styles.size(), 2u);
    EXPECT_EQ(scene.layer->styles[0]->id, firstId);
    EXPECT_EQ(scene.layer->styles[1]->id, secondId);

    scene.undo.redo(scene.document);
    ASSERT_EQ(scene.layer->styles.size(), 1u);
    EXPECT_EQ(scene.layer->styles[0]->id, secondId);
}

TEST(RemoveStyleCommandTest, OutOfRangeIsNoOp) {
    Scene scene;
    scene.layer->styles.push_back(std::make_unique<FillStyle>());

    scene.execute<RemoveStyleCommand>(scene.layer->id, 5);
    EXPECT_EQ(scene.layer->styles.size(), 1u);
    scene.undo.undo(scene.document);
    EXPECT_EQ(scene.layer->styles.size(), 1u);
}

TEST(RemoveStyleCommandTest, ExecuteSkipsMissingLayer) {
    Scene scene;
    scene.execute<RemoveStyleCommand>(EntityId{999}, 0);
    scene.undo.undo(scene.document);
    EXPECT_TRUE(scene.layer->styles.empty());
}

TEST(SetStyleBlendModeCommandTest, SetAndUndo) {
    Scene scene;
    scene.layer->styles.push_back(std::make_unique<FillStyle>());

    scene.execute<SetStyleBlendModeCommand>(scene.layer->id, 0, motion::BlendMode::Multiply);
    auto *fill = static_cast<FillStyle *>(scene.layer->styles[0].get());
    EXPECT_EQ(fill->blendMode, motion::BlendMode::Multiply);

    scene.undo.undo(scene.document);
    EXPECT_EQ(fill->blendMode, motion::BlendMode::Normal);

    scene.undo.redo(scene.document);
    EXPECT_EQ(fill->blendMode, motion::BlendMode::Multiply);
}

TEST(SetStyleBlendModeCommandTest, MergesSameTargetKeepsOriginalValue) {
    Scene scene;
    scene.layer->styles.push_back(std::make_unique<FillStyle>());
    auto *fill = static_cast<FillStyle *>(scene.layer->styles[0].get());

    scene.execute<SetStyleBlendModeCommand>(scene.layer->id, 0, motion::BlendMode::Multiply);
    scene.execute<SetStyleBlendModeCommand>(scene.layer->id, 0, motion::BlendMode::Screen);
    EXPECT_EQ(fill->blendMode, motion::BlendMode::Screen);

    scene.undo.undo(scene.document);
    EXPECT_EQ(fill->blendMode, motion::BlendMode::Normal);
}

TEST(SetStyleBlendModeCommandTest, StrokeStyleBlendSetAndUndo) {
    Scene scene;
    scene.layer->styles.push_back(std::make_unique<motion::StrokeStyle>());
    auto *stroke = static_cast<motion::StrokeStyle *>(scene.layer->styles[0].get());

    scene.execute<SetStyleBlendModeCommand>(scene.layer->id, 0, motion::BlendMode::Multiply);
    EXPECT_EQ(stroke->blendMode, motion::BlendMode::Multiply);

    scene.undo.undo(scene.document);
    EXPECT_EQ(stroke->blendMode, motion::BlendMode::Normal);
}

TEST(SetStyleBlendModeCommandTest, ExecuteSkipsMissingLayer) {
    Scene scene;
    scene.execute<SetStyleBlendModeCommand>(EntityId{999}, 0, motion::BlendMode::Add);
    scene.undo.undo(scene.document);
    EXPECT_TRUE(scene.layer->styles.empty());
}

TEST(AddLayerStyleCommandTest, AddStrokeUndoRedo) {
    Scene scene;

    scene.execute<AddLayerStyleCommand>(scene.layer->id,
                                        std::make_unique<motion::StrokeStyle>());
    ASSERT_EQ(scene.layer->styles.size(), 1u);
    EXPECT_EQ(scene.layer->styles[0]->type(), motion::LayerStyleType::Stroke);
    const EntityId styleId = scene.layer->styles[0]->id;

    scene.undo.undo(scene.document);
    EXPECT_TRUE(scene.layer->styles.empty());

    scene.undo.redo(scene.document);
    ASSERT_EQ(scene.layer->styles.size(), 1u);
    EXPECT_EQ(scene.layer->styles[0]->id, styleId);
}

TEST(SetStrokePositionCommandTest, SetAndUndo) {
    Scene scene;
    scene.layer->styles.push_back(std::make_unique<motion::StrokeStyle>());
    auto *stroke = static_cast<motion::StrokeStyle *>(scene.layer->styles[0].get());

    scene.execute<SetStrokePositionCommand>(scene.layer->id, 0,
                                            motion::StrokePosition::Inside);
    EXPECT_EQ(stroke->position, motion::StrokePosition::Inside);

    scene.undo.undo(scene.document);
    EXPECT_EQ(stroke->position, motion::StrokePosition::Center);

    scene.undo.redo(scene.document);
    EXPECT_EQ(stroke->position, motion::StrokePosition::Inside);
}

TEST(SetStrokePositionCommandTest, MergesSameTargetKeepsOriginalValue) {
    Scene scene;
    scene.layer->styles.push_back(std::make_unique<motion::StrokeStyle>());
    auto *stroke = static_cast<motion::StrokeStyle *>(scene.layer->styles[0].get());

    scene.execute<SetStrokePositionCommand>(scene.layer->id, 0,
                                            motion::StrokePosition::Inside);
    scene.execute<SetStrokePositionCommand>(scene.layer->id, 0,
                                            motion::StrokePosition::Outside);
    EXPECT_EQ(stroke->position, motion::StrokePosition::Outside);

    scene.undo.undo(scene.document);
    EXPECT_EQ(stroke->position, motion::StrokePosition::Center);
}

TEST(SetStrokePositionCommandTest, NonStrokeStyleIsNoOp) {
    Scene scene;
    scene.layer->styles.push_back(std::make_unique<FillStyle>());

    scene.execute<SetStrokePositionCommand>(scene.layer->id, 0,
                                            motion::StrokePosition::Outside);
    scene.undo.undo(scene.document);
    ASSERT_EQ(scene.layer->styles.size(), 1u);
    EXPECT_EQ(scene.layer->styles[0]->type(), motion::LayerStyleType::Fill);
}

TEST(SetStrokePositionCommandTest, ExecuteSkipsMissingLayer) {
    Scene scene;
    scene.execute<SetStrokePositionCommand>(EntityId{999}, 0, motion::StrokePosition::Inside);
    scene.undo.undo(scene.document);
    EXPECT_TRUE(scene.layer->styles.empty());
}

namespace {

Mask MakeTestMask(MaskMode mode) {
    Mask mask;
    mask.mode = mode;
    BezierPath path;
    path.closed = true;
    path.vertices.push_back({{0, 0}, {}, {}});
    path.vertices.push_back({{10, 0}, {}, {}});
    path.vertices.push_back({{10, 10}, {}, {}});
    mask.path.setStaticValue(path);
    return mask;
}

}  // namespace

TEST(AddMaskCommandTest, AddUndoRedo) {
    Scene scene;
    scene.execute<AddMaskCommand>(scene.layer->id, MakeTestMask(MaskMode::Subtract));
    ASSERT_EQ(scene.layer->masks.size(), 1u);
    EXPECT_EQ(scene.layer->masks[0].mode, MaskMode::Subtract);

    scene.undo.undo(scene.document);
    EXPECT_TRUE(scene.layer->masks.empty());

    scene.undo.redo(scene.document);
    ASSERT_EQ(scene.layer->masks.size(), 1u);
    EXPECT_EQ(scene.layer->masks[0].mode, MaskMode::Subtract);
}

TEST(RemoveMaskCommandTest, RemoveUndoRedo) {
    Scene scene;
    scene.layer->masks.push_back(MakeTestMask(MaskMode::Add));
    scene.layer->masks.push_back(MakeTestMask(MaskMode::Intersect));

    scene.execute<RemoveMaskCommand>(scene.layer->id, 0);
    ASSERT_EQ(scene.layer->masks.size(), 1u);
    EXPECT_EQ(scene.layer->masks[0].mode, MaskMode::Intersect);

    scene.undo.undo(scene.document);
    ASSERT_EQ(scene.layer->masks.size(), 2u);
    EXPECT_EQ(scene.layer->masks[0].mode, MaskMode::Add);
}

TEST(MoveMaskCommandTest, ReordersMasks) {
    Scene scene;
    scene.layer->masks.push_back(MakeTestMask(MaskMode::Add));
    scene.layer->masks.push_back(MakeTestMask(MaskMode::Subtract));
    scene.layer->masks.push_back(MakeTestMask(MaskMode::Intersect));

    scene.execute<MoveMaskCommand>(scene.layer->id, 0, 2);
    EXPECT_EQ(scene.layer->masks[0].mode, MaskMode::Subtract);
    EXPECT_EQ(scene.layer->masks[2].mode, MaskMode::Add);

    scene.undo.undo(scene.document);
    EXPECT_EQ(scene.layer->masks[0].mode, MaskMode::Add);
}

TEST(SetMaskModeCommandTest, SetAndMerge) {
    Scene scene;
    scene.layer->masks.push_back(MakeTestMask(MaskMode::Add));

    scene.execute<SetMaskModeCommand>(scene.layer->id, 0, MaskMode::Subtract);
    scene.execute<SetMaskModeCommand>(scene.layer->id, 0, MaskMode::Intersect);
    EXPECT_EQ(scene.layer->masks[0].mode, MaskMode::Intersect);

    scene.undo.undo(scene.document);
    EXPECT_EQ(scene.layer->masks[0].mode, MaskMode::Add);
}

TEST(SetMaskInvertedCommandTest, SetAndUndo) {
    Scene scene;
    scene.layer->masks.push_back(MakeTestMask(MaskMode::Add));

    scene.execute<SetMaskInvertedCommand>(scene.layer->id, 0, true);
    EXPECT_TRUE(scene.layer->masks[0].inverted);

    scene.undo.undo(scene.document);
    EXPECT_FALSE(scene.layer->masks[0].inverted);
}

TEST(SetTrackMatteCommandTest, SetAndClear) {
    Scene scene;
    Layer *matte =
        scene.document.addLayer(scene.composition->id, std::make_unique<Layer>(LayerType::Shape));

    scene.execute<SetTrackMatteCommand>(scene.layer->id, matte->id, TrackMatteType::Alpha);
    EXPECT_EQ(scene.layer->trackMatteType, TrackMatteType::Alpha);
    EXPECT_EQ(scene.layer->trackMatteLayerId, matte->id);

    // Close the timed merge window so clear is a separate undo unit.
    scene.undo.endMergeGroup();

    scene.execute<SetTrackMatteCommand>(scene.layer->id, EntityId{}, TrackMatteType::None);
    EXPECT_EQ(scene.layer->trackMatteType, TrackMatteType::None);
    EXPECT_FALSE(scene.layer->trackMatteLayerId.isValid());

    scene.undo.undo(scene.document);
    EXPECT_EQ(scene.layer->trackMatteType, TrackMatteType::Alpha);
}

TEST(SetTrackMatteCommandTest, ConsecutiveSetsMerge) {
    Scene scene;
    Layer *matte =
        scene.document.addLayer(scene.composition->id, std::make_unique<Layer>(LayerType::Shape));

    scene.execute<SetTrackMatteCommand>(scene.layer->id, matte->id, TrackMatteType::Alpha);
    scene.execute<SetTrackMatteCommand>(scene.layer->id, matte->id, TrackMatteType::Luma);
    EXPECT_EQ(scene.layer->trackMatteType, TrackMatteType::Luma);

    scene.undo.undo(scene.document);
    EXPECT_EQ(scene.layer->trackMatteType, TrackMatteType::None);
    EXPECT_FALSE(scene.layer->trackMatteLayerId.isValid());
}
