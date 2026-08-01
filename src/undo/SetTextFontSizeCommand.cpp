#include "MotionStudio/undo/SetTextFontSizeCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/TextContent.h"

namespace motion {

SetTextFontSizeCommand::SetTextFontSizeCommand(EntityId layerId, float fontSize)
    : layerId_(layerId)
    , fontSize_(fontSize) {
}

void SetTextFontSizeCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->type() != LayerType::Text) {
        return;
    }
    auto *content = static_cast<TextContent *>(layer->content.get());
    if (!oldFontSize_) {
        oldFontSize_ = content->fontSize;
    }
    content->fontSize = fontSize_;
}

void SetTextFontSizeCommand::undo(Document &document) {
    if (!oldFontSize_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->type() != LayerType::Text) {
        return;
    }
    static_cast<TextContent *>(layer->content.get())->fontSize = *oldFontSize_;
}

bool SetTextFontSizeCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetTextFontSize) {
        return false;
    }
    const auto &typed = static_cast<const SetTextFontSizeCommand &>(other);
    if (typed.layerId_ != layerId_) {
        return false;
    }
    fontSize_ = typed.fontSize_;
    return true;
}

CommandKind SetTextFontSizeCommand::kind() const {
    return CommandKind::SetTextFontSize;
}

std::string SetTextFontSizeCommand::describe() const {
    return "Set Text Font Size";
}

}  // namespace motion
