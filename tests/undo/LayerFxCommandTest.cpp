#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/LayerFx.h"
#include "MotionStudio/undo/AddLayerFxCommand.h"
#include "MotionStudio/undo/MoveLayerFxCommand.h"
#include "MotionStudio/undo/RemoveLayerFxCommand.h"
#include "MotionStudio/undo/SetLayerFxBlendModeCommand.h"
#include "MotionStudio/undo/SetLayerFxEnabledCommand.h"
#include "MotionStudio/undo/SetLayerFxStrokePositionCommand.h"
#include "MotionStudio/undo/UndoManager.h"

using motion::AddLayerFxCommand;
using motion::BlendMode;
using motion::Composition;
using motion::Document;
using motion::DropShadowStyle;
using motion::EntityId;
using motion::Layer;
using motion::LayerStrokeStyle;
using motion::LayerType;
using motion::MoveLayerFxCommand;
using motion::OuterGlowStyle;
using motion::RemoveLayerFxCommand;
using motion::SetLayerFxBlendModeCommand;
using motion::SetLayerFxEnabledCommand;
using motion::SetLayerFxStrokePositionCommand;
using motion::StrokePosition;
using motion::UndoManager;

namespace {

struct Scene {
    Document document;
    UndoManager undo;
    Composition *composition = nullptr;
    Layer *layer = nullptr;

    Scene() {
        composition = document.addComposition(std::make_unique<Composition>());
        layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    }

    template <typename CommandType, typename... Args>
    void execute(Args &&...args) {
        undo.execute(document, std::make_unique<CommandType>(std::forward<Args>(args)...));
    }
};

}  // namespace

TEST(LayerFxCommandTest, AddUndoRedoKeepsId) {
    Scene scene;
    auto style = std::make_unique<DropShadowStyle>();
    const EntityId styleId = style->id;
    scene.execute<AddLayerFxCommand>(scene.layer->id, std::move(style));
    ASSERT_EQ(scene.layer->layerStyles.size(), 1u);
    EXPECT_EQ(scene.layer->layerStyles[0]->id, styleId);

    scene.undo.undo(scene.document);
    EXPECT_TRUE(scene.layer->layerStyles.empty());

    scene.undo.redo(scene.document);
    ASSERT_EQ(scene.layer->layerStyles.size(), 1u);
    EXPECT_EQ(scene.layer->layerStyles[0]->id, styleId);
}

TEST(LayerFxCommandTest, RemoveUndoRedoRestoresIndex) {
    Scene scene;
    auto first = std::make_unique<DropShadowStyle>();
    auto second = std::make_unique<OuterGlowStyle>();
    const EntityId firstId = first->id;
    const EntityId secondId = second->id;
    scene.layer->layerStyles.push_back(std::move(first));
    scene.layer->layerStyles.push_back(std::move(second));

    scene.execute<RemoveLayerFxCommand>(scene.layer->id, 0);
    ASSERT_EQ(scene.layer->layerStyles.size(), 1u);
    EXPECT_EQ(scene.layer->layerStyles[0]->id, secondId);

    scene.undo.undo(scene.document);
    ASSERT_EQ(scene.layer->layerStyles.size(), 2u);
    EXPECT_EQ(scene.layer->layerStyles[0]->id, firstId);
    EXPECT_EQ(scene.layer->layerStyles[1]->id, secondId);
}

TEST(LayerFxCommandTest, MoveReordersAndUndoRestores) {
    Scene scene;
    auto shadow = std::make_unique<DropShadowStyle>();
    auto glow = std::make_unique<OuterGlowStyle>();
    const EntityId shadowId = shadow->id;
    const EntityId glowId = glow->id;
    scene.layer->layerStyles.push_back(std::move(shadow));
    scene.layer->layerStyles.push_back(std::move(glow));

    scene.execute<MoveLayerFxCommand>(scene.layer->id, 0, 1);
    EXPECT_EQ(scene.layer->layerStyles[0]->id, glowId);
    EXPECT_EQ(scene.layer->layerStyles[1]->id, shadowId);

    scene.undo.undo(scene.document);
    EXPECT_EQ(scene.layer->layerStyles[0]->id, shadowId);
    EXPECT_EQ(scene.layer->layerStyles[1]->id, glowId);
}

TEST(LayerFxCommandTest, SetEnabledAndUndo) {
    Scene scene;
    scene.layer->layerStyles.push_back(std::make_unique<DropShadowStyle>());
    EXPECT_TRUE(scene.layer->layerStyles[0]->enabled);

    scene.execute<SetLayerFxEnabledCommand>(scene.layer->id, 0, false);
    EXPECT_FALSE(scene.layer->layerStyles[0]->enabled);

    scene.undo.undo(scene.document);
    EXPECT_TRUE(scene.layer->layerStyles[0]->enabled);
}

TEST(LayerFxCommandTest, SetBlendModeAndUndo) {
    Scene scene;
    scene.layer->layerStyles.push_back(std::make_unique<DropShadowStyle>());
    auto *shadow = static_cast<DropShadowStyle *>(scene.layer->layerStyles[0].get());
    EXPECT_EQ(shadow->blendMode, BlendMode::Multiply);

    scene.execute<SetLayerFxBlendModeCommand>(scene.layer->id, 0, BlendMode::Normal);
    EXPECT_EQ(shadow->blendMode, BlendMode::Normal);

    scene.undo.undo(scene.document);
    EXPECT_EQ(shadow->blendMode, BlendMode::Multiply);
}

TEST(LayerFxCommandTest, SetStrokePositionAndUndo) {
    Scene scene;
    scene.layer->layerStyles.push_back(std::make_unique<LayerStrokeStyle>());
    auto *stroke = static_cast<LayerStrokeStyle *>(scene.layer->layerStyles[0].get());
    EXPECT_EQ(stroke->position, StrokePosition::Outside);

    scene.execute<SetLayerFxStrokePositionCommand>(scene.layer->id, 0, StrokePosition::Inside);
    EXPECT_EQ(stroke->position, StrokePosition::Inside);

    scene.undo.undo(scene.document);
    EXPECT_EQ(stroke->position, StrokePosition::Outside);
}

TEST(LayerFxCommandTest, SetStrokePositionOnDropShadowIsNoOp) {
    Scene scene;
    scene.layer->layerStyles.push_back(std::make_unique<DropShadowStyle>());
    auto *shadow = static_cast<DropShadowStyle *>(scene.layer->layerStyles[0].get());
    const BlendMode blend = shadow->blendMode;

    scene.execute<SetLayerFxStrokePositionCommand>(scene.layer->id, 0, StrokePosition::Inside);
    EXPECT_EQ(shadow->blendMode, blend);
    EXPECT_TRUE(shadow->enabled);
}
