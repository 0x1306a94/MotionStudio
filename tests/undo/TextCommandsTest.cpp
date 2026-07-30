#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/model/Asset.h"
#include "MotionStudio/model/AssetType.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/TextAlign.h"
#include "MotionStudio/model/TextContent.h"
#include "MotionStudio/undo/ImportFontAssetCommand.h"
#include "MotionStudio/undo/SetTextAlignCommand.h"
#include "MotionStudio/undo/SetTextAutoHeightCommand.h"
#include "MotionStudio/undo/SetTextFontAssetCommand.h"
#include "MotionStudio/undo/SetTextFontFamilyCommand.h"
#include "MotionStudio/undo/UndoManager.h"

using motion::Asset;
using motion::AssetType;
using motion::Composition;
using motion::Document;
using motion::EntityId;
using motion::ImportFontAssetCommand;
using motion::Layer;
using motion::LayerType;
using motion::SetTextAlignCommand;
using motion::SetTextAutoHeightCommand;
using motion::SetTextFontAssetCommand;
using motion::SetTextFontFamilyCommand;
using motion::TextAlign;
using motion::TextContent;
using motion::UndoManager;

namespace {

TextContent *AddTextLayer(Document &document) {
    Composition *composition = document.addComposition(std::make_unique<Composition>());
    Layer *layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Text));
    return static_cast<TextContent *>(layer->content.get());
}

}  // namespace

TEST(TextCommandsTest, AutoHeightAlignFontFamilyUndo) {
    Document document;
    TextContent *content = AddTextLayer(document);
    const EntityId layerId = document.compositions[0]->layers[0]->id;
    UndoManager undo;

    undo.execute(document, std::make_unique<SetTextAutoHeightCommand>(layerId, false));
    EXPECT_FALSE(content->autoHeight);
    undo.execute(document, std::make_unique<SetTextAlignCommand>(layerId, TextAlign::Right));
    EXPECT_EQ(content->align, TextAlign::Right);
    undo.execute(document,
                 std::make_unique<SetTextFontFamilyCommand>(layerId, std::string{"Helvetica"}));
    EXPECT_EQ(content->fontFamily, "Helvetica");

    undo.undo(document);
    EXPECT_EQ(content->fontFamily, "PingFang SC");
    undo.undo(document);
    EXPECT_EQ(content->align, TextAlign::Left);
    undo.undo(document);
    EXPECT_TRUE(content->autoHeight);
}

TEST(TextCommandsTest, ImportAndBindFontAsset) {
    Document document;
    TextContent *content = AddTextLayer(document);
    const EntityId layerId = document.compositions[0]->layers[0]->id;
    UndoManager undo;

    Asset font;
    font.id = EntityId::Generate();
    font.type = AssetType::Font;
    font.name = "CustomFont";
    font.path = "assets/CustomFont.ttf";
    undo.execute(document, std::make_unique<ImportFontAssetCommand>(font));
    ASSERT_EQ(document.assets.size(), 1u);
    EXPECT_EQ(document.assets[0].type, AssetType::Font);

    undo.execute(document, std::make_unique<SetTextFontAssetCommand>(layerId, font.id));
    EXPECT_EQ(content->fontAssetId, font.id);
    EXPECT_EQ(content->fontFamily, "CustomFont");

    undo.undo(document);
    EXPECT_FALSE(content->fontAssetId.isValid());
    EXPECT_EQ(content->fontFamily, "PingFang SC");

    undo.undo(document);
    EXPECT_TRUE(document.assets.empty());
}
