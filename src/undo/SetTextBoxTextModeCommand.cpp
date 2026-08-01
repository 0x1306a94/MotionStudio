#include "MotionStudio/undo/SetTextBoxTextModeCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/TextContent.h"

namespace motion {

SetTextBoxTextModeCommand::SetTextBoxTextModeCommand(EntityId layerId, bool boxTextMode,
                                                     std::optional<Vec2> sizeWhenEnabling)
    : layerId_(layerId)
    , boxTextMode_(boxTextMode)
    , sizeWhenEnabling_(std::move(sizeWhenEnabling)) {
}

void SetTextBoxTextModeCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->type() != LayerType::Text) {
        return;
    }
    auto *content = static_cast<TextContent *>(layer->content.get());
    if (!oldBoxTextMode_) {
        oldBoxTextMode_ = content->boxTextMode;
        oldSize_ = content->size;
    }
    if (boxTextMode_ && sizeWhenEnabling_) {
        content->size = *sizeWhenEnabling_;
    }
    content->boxTextMode = boxTextMode_;
}

void SetTextBoxTextModeCommand::undo(Document &document) {
    if (!oldBoxTextMode_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->type() != LayerType::Text) {
        return;
    }
    auto *content = static_cast<TextContent *>(layer->content.get());
    content->boxTextMode = *oldBoxTextMode_;
    if (oldSize_) {
        content->size = *oldSize_;
    }
}

bool SetTextBoxTextModeCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetTextBoxTextMode) {
        return false;
    }
    const auto &typed = static_cast<const SetTextBoxTextModeCommand &>(other);
    if (typed.layerId_ != layerId_) {
        return false;
    }
    boxTextMode_ = typed.boxTextMode_;
    sizeWhenEnabling_ = typed.sizeWhenEnabling_;
    return true;
}

CommandKind SetTextBoxTextModeCommand::kind() const {
    return CommandKind::SetTextBoxTextMode;
}

std::string SetTextBoxTextModeCommand::describe() const {
    return "Set Text Box Text Mode";
}

}  // namespace motion
