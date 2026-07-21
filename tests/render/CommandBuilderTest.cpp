#include <gtest/gtest.h>

#include "MotionStudio/render/CommandBuilder.h"

using motion::BlendMode;
using motion::BuildCommands;
using motion::Color;
using motion::DrawCommandType;
using motion::EvaluatedLayer;
using motion::EvaluatedShapeItem;
using motion::LineCap;
using motion::Paint;
using motion::SceneState;

namespace {

EvaluatedShapeItem MakeFillItem() {
    EvaluatedShapeItem item;
    item.path.closed = true;
    item.path.vertices.push_back({{0, 0}, {}, {}});
    item.path.vertices.push_back({{10, 0}, {}, {}});
    item.paint = Paint{Color{1, 0, 0, 1}};
    return item;
}

EvaluatedShapeItem MakeStrokeItem() {
    EvaluatedShapeItem item;
    item.isStroke = true;
    item.path.vertices.push_back({{0, 0}, {}, {}});
    item.path.vertices.push_back({{10, 10}, {}, {}});
    item.paint = Paint{Color{0, 0, 1, 1}};
    item.strokeWidth = 3;
    item.cap = LineCap::Round;
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
    layer.shapeItems.push_back(MakeFillItem());
    layer.shapeItems.push_back(MakeStrokeItem());
    state.layers.push_back(std::move(layer));

    auto commands = BuildCommands(state);
    ASSERT_EQ(commands.size(), 6u);
    EXPECT_EQ(commands[0].type, DrawCommandType::Save);
    EXPECT_EQ(commands[1].type, DrawCommandType::SetOpacity);
    EXPECT_FLOAT_EQ(commands[1].opacity, 0.5f);
    EXPECT_EQ(commands[2].type, DrawCommandType::SetBlendMode);
    EXPECT_EQ(commands[2].blendMode, BlendMode::Screen);
    EXPECT_EQ(commands[3].type, DrawCommandType::DrawPath);
    EXPECT_EQ(commands[3].paint.color, (Color{1, 0, 0, 1}));
    EXPECT_EQ(commands[4].type, DrawCommandType::StrokePath);
    EXPECT_EQ(commands[5].type, DrawCommandType::Restore);
}

TEST(CommandBuilderTest, StrokeItemCarriesStrokeParameters) {
    SceneState state;
    EvaluatedLayer layer;
    layer.shapeItems.push_back(MakeStrokeItem());
    state.layers.push_back(std::move(layer));

    auto commands = BuildCommands(state);
    ASSERT_EQ(commands.size(), 5u);
    EXPECT_EQ(commands[3].type, DrawCommandType::StrokePath);
    EXPECT_FLOAT_EQ(commands[3].strokeWidth, 3.0f);
    EXPECT_EQ(commands[3].cap, LineCap::Round);
    EXPECT_EQ(commands[3].paint.color, (Color{0, 0, 1, 1}));
}

TEST(CommandBuilderTest, MultipleLayersKeepRenderOrder) {
    SceneState state;
    state.layers.push_back(EvaluatedLayer{});
    state.layers.push_back(EvaluatedLayer{});

    auto commands = BuildCommands(state);
    // Two empty layers: (Save/SetOpacity/SetBlendMode/Restore) x 2.
    ASSERT_EQ(commands.size(), 8u);
    EXPECT_EQ(commands[0].type, DrawCommandType::Save);
    EXPECT_EQ(commands[3].type, DrawCommandType::Restore);
    EXPECT_EQ(commands[4].type, DrawCommandType::Save);
    EXPECT_EQ(commands[7].type, DrawCommandType::Restore);
}
