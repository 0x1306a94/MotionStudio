#include <memory>

#include <gtest/gtest.h>

#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/TextAlign.h"
#include "MotionStudio/model/TextContent.h"
#include "MotionStudio/undo/SetTextAlignCommand.h"
#include "MotionStudio/undo/SetTextAutoHeightCommand.h"
#include "MotionStudio/undo/SetTextFontFamilyCommand.h"
#include "MotionStudio/undo/UndoManager.h"

using motion::Composition;
using motion::Document;
using motion::EntityId;
using motion::Layer;
using motion::LayerType;
using motion::SetTextAlignCommand;
using motion::SetTextAutoHeightCommand;
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
