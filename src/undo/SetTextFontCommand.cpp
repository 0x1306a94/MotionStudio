#include "MotionStudio/undo/SetTextFontCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/TextContent.h"

namespace motion {

SetTextFontCommand::SetTextFontCommand(EntityId layerId, std::string fontFamily, std::string fontStyle)
    : layerId_(layerId)
    , fontFamily_(std::move(fontFamily))
    , fontStyle_(std::move(fontStyle)) {
}

void SetTextFontCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->type() != LayerType::Text) {
        return;
    }
    auto *content = static_cast<TextContent *>(layer->content.get());
    if (!oldFontFamily_) {
        oldFontFamily_ = content->fontFamily;
        oldFontStyle_ = content->fontStyle;
    }
    content->fontFamily = fontFamily_;
    content->fontStyle = fontStyle_;
}

void SetTextFontCommand::undo(Document &document) {
    if (!oldFontFamily_ || !oldFontStyle_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->type() != LayerType::Text) {
        return;
    }
    auto *content = static_cast<TextContent *>(layer->content.get());
    content->fontFamily = *oldFontFamily_;
    content->fontStyle = *oldFontStyle_;
}

bool SetTextFontCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetTextFont) {
        return false;
    }
    const auto &typed = static_cast<const SetTextFontCommand &>(other);
    if (typed.layerId_ != layerId_) {
        return false;
    }
    fontFamily_ = typed.fontFamily_;
    fontStyle_ = typed.fontStyle_;
    return true;
}

CommandKind SetTextFontCommand::kind() const {
    return CommandKind::SetTextFont;
}

std::string SetTextFontCommand::describe() const {
    return "Set Text Font";
}

}  // namespace motion
