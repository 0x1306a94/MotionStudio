#include "MotionStudio/undo/SetTextFontFamilyCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/TextContent.h"

namespace motion {

SetTextFontFamilyCommand::SetTextFontFamilyCommand(EntityId layerId, std::string fontFamily)
    : layerId_(layerId)
    , fontFamily_(std::move(fontFamily)) {
}

void SetTextFontFamilyCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->type() != LayerType::Text) {
        return;
    }
    auto *content = static_cast<TextContent *>(layer->content.get());
    if (!oldFontFamily_) {
        oldFontFamily_ = content->fontFamily;
    }
    content->fontFamily = fontFamily_;
}

void SetTextFontFamilyCommand::undo(Document &document) {
    if (!oldFontFamily_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->type() != LayerType::Text) {
        return;
    }
    static_cast<TextContent *>(layer->content.get())->fontFamily = *oldFontFamily_;
}

bool SetTextFontFamilyCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetTextFontFamily) {
        return false;
    }
    const auto &typed = static_cast<const SetTextFontFamilyCommand &>(other);
    if (typed.layerId_ != layerId_) {
        return false;
    }
    fontFamily_ = typed.fontFamily_;
    return true;
}

CommandKind SetTextFontFamilyCommand::kind() const {
    return CommandKind::SetTextFontFamily;
}

std::string SetTextFontFamilyCommand::describe() const {
    return "Set Text Font Family";
}

}  // namespace motion
