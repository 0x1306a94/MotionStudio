#include <gtest/gtest.h>

#include "MotionStudio/render/CommandBuilder.h"
#include "MotionStudio/render/ShapeGeometry.h"

using motion::BezierPath;
using motion::BlendMode;
using motion::BuildCommands;
using motion::BuildSelectionOutlineCommands;
using motion::Color;
using motion::DrawCommandType;
using motion::EntityId;
using motion::EvaluatedLayer;
using motion::EvaluatedShapeItem;
using motion::LineCap;
using motion::LineJoin;
using motion::MakePathGeometry;
using motion::Paint;
using motion::SceneState;
using motion::ShapeGeometryKind;

namespace {

EvaluatedShapeItem MakeFillItem(BlendMode blendMode = BlendMode::Normal) {
    EvaluatedShapeItem item;
    BezierPath path;
    path.closed = true;
    path.vertices.push_back({{0, 0}, {}, {}});
    path.vertices.push_back({{10, 0}, {}, {}});
    item.geometry = MakePathGeometry(std::move(path));
    item.paint = Paint{Color{1, 0, 0, 1}, motion::FillRule::NonZero, blendMode};
    return item;
}

EvaluatedShapeItem MakeStrokeItem(BlendMode blendMode = BlendMode::Normal) {
    EvaluatedShapeItem item;
    item.isStroke = true;
    BezierPath path;
    path.vertices.push_back({{0, 0}, {}, {}});
    path.vertices.push_back({{10, 10}, {}, {}});
    item.geometry = MakePathGeometry(std::move(path));
    item.paint = Paint{Color{0, 0, 1, 1}, motion::FillRule::NonZero, blendMode};
    item.stroke.width = 3;
    item.stroke.cap = LineCap::Round;
    return item;
}

}  // namespace

TEST(CommandBuilderTest, EmptySceneProducesNoCommands) {
    SceneState state;
    EXPECT_TRUE(BuildCommands(state).empty());
}

TEST(CommandBuilderTest, LayerExpandsToScopedDrawSequence) {
    SceneState state;
    EvaluatedLayer layer;
    layer.opacity = 0.5f;
    layer.blendMode = BlendMode::Screen;
    layer.shapeItems.push_back(MakeFillItem(BlendMode::Screen));
    layer.shapeItems.push_back(MakeStrokeItem(BlendMode::Screen));
    state.layers.push_back(std::move(layer));

    auto commands = BuildCommands(state);
    ASSERT_EQ(commands.size(), 7u);
    EXPECT_EQ(commands[0].type, DrawCommandType::Save);
    EXPECT_EQ(commands[1].type, DrawCommandType::ConcatTransform);
    EXPECT_EQ(commands[2].type, DrawCommandType::SetOpacity);
    EXPECT_FLOAT_EQ(commands[2].opacity, 0.5f);
    EXPECT_EQ(commands[3].type, DrawCommandType::SetBlendMode);
    EXPECT_EQ(commands[3].blendMode, BlendMode::Screen);
    EXPECT_EQ(commands[4].type, DrawCommandType::DrawPath);
    EXPECT_EQ(commands[4].paint.color, (Color{1, 0, 0, 1}));
    EXPECT_EQ(commands[5].type, DrawCommandType::StrokePath);
    EXPECT_EQ(commands[6].type, DrawCommandType::Restore);
}

TEST(CommandBuilderTest, ItemBlendModeEmitsSetBlendModeOnChange) {
    SceneState state;
    EvaluatedLayer layer;
    layer.shapeItems.push_back(MakeFillItem(BlendMode::Add));
    layer.shapeItems.push_back(MakeStrokeItem());
    state.layers.push_back(std::move(layer));

    auto commands = BuildCommands(state);
    // Layer-level SetBlendMode, then a blend switch before each diverging item.
    ASSERT_EQ(commands.size(), 9u);
    EXPECT_EQ(commands[3].type, DrawCommandType::SetBlendMode);
    EXPECT_EQ(commands[3].blendMode, BlendMode::Normal);
    EXPECT_EQ(commands[4].type, DrawCommandType::SetBlendMode);
    EXPECT_EQ(commands[4].blendMode, BlendMode::Add);
    EXPECT_EQ(commands[5].type, DrawCommandType::DrawPath);
    EXPECT_EQ(commands[6].type, DrawCommandType::SetBlendMode);
    EXPECT_EQ(commands[6].blendMode, BlendMode::Normal);
    EXPECT_EQ(commands[7].type, DrawCommandType::StrokePath);
    EXPECT_EQ(commands[8].type, DrawCommandType::Restore);
}

TEST(CommandBuilderTest, StrokeItemCarriesStrokeParameters) {
    SceneState state;
    EvaluatedLayer layer;
    layer.shapeItems.push_back(MakeStrokeItem());
    state.layers.push_back(std::move(layer));

    auto commands = BuildCommands(state);
    ASSERT_EQ(commands.size(), 6u);
    EXPECT_EQ(commands[4].type, DrawCommandType::StrokePath);
    EXPECT_FLOAT_EQ(commands[4].stroke.width, 3.0f);
    EXPECT_EQ(commands[4].stroke.cap, LineCap::Round);
    EXPECT_EQ(commands[4].paint.color, (Color{0, 0, 1, 1}));
}

TEST(CommandBuilderTest, MultipleLayersKeepRenderOrder) {
    SceneState state;
    state.layers.push_back(EvaluatedLayer{});
    state.layers.push_back(EvaluatedLayer{});

    auto commands = BuildCommands(state);
    // Two empty layers: (Save/ConcatTransform/SetOpacity/SetBlendMode/Restore) x 2.
    ASSERT_EQ(commands.size(), 10u);
    EXPECT_EQ(commands[0].type, DrawCommandType::Save);
    EXPECT_EQ(commands[4].type, DrawCommandType::Restore);
    EXPECT_EQ(commands[5].type, DrawCommandType::Save);
    EXPECT_EQ(commands[9].type, DrawCommandType::Restore);
}

TEST(CommandBuilderTest, SelectionOutlineBuildsStrokeForSelectedLayerBounds) {
    SceneState state;
    EvaluatedLayer layer;
    layer.id = EntityId{42};
    layer.shapeItems.push_back(MakeFillItem());
    state.layers.push_back(std::move(layer));

    auto commands = BuildSelectionOutlineCommands(state, {EntityId{42}}, 1.5f);

    ASSERT_EQ(commands.size(), 1u);
    EXPECT_EQ(commands[0].type, DrawCommandType::StrokePath);
    EXPECT_EQ(commands[0].paint.color, (Color{0.0f, 0.47843137f, 1.0f, 1.0f}));
    EXPECT_FLOAT_EQ(commands[0].stroke.width, 1.5f);
    EXPECT_EQ(commands[0].stroke.join, LineJoin::Round);
    EXPECT_EQ(commands[0].geometry.kind, ShapeGeometryKind::Rect);
    EXPECT_FLOAT_EQ(commands[0].geometry.center.x, 5.0f);
    EXPECT_FLOAT_EQ(commands[0].geometry.center.y, 0.0f);
    EXPECT_FLOAT_EQ(commands[0].geometry.size.x, 11.5f);
    EXPECT_FLOAT_EQ(commands[0].geometry.size.y, 1.5f);
}

TEST(CommandBuilderTest, SelectionOutlineSkipsMissingLayers) {
    SceneState state;
    EvaluatedLayer layer;
    layer.id = EntityId{42};
    layer.shapeItems.push_back(MakeFillItem());
    state.layers.push_back(std::move(layer));

    EXPECT_TRUE(BuildSelectionOutlineCommands(state, {EntityId{7}}, 1.5f).empty());
}
