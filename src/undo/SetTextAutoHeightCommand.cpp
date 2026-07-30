#include "MotionStudio/undo/SetTextAutoHeightCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/TextContent.h"

namespace motion {

SetTextAutoHeightCommand::SetTextAutoHeightCommand(EntityId layerId, bool autoHeight)
    : layerId_(layerId)
    , autoHeight_(autoHeight) {
}

void SetTextAutoHeightCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->type() != LayerType::Text) {
        return;
    }
    auto *content = static_cast<TextContent *>(layer->content.get());
    if (!oldAutoHeight_) {
        oldAutoHeight_ = content->autoHeight;
    }
    content->autoHeight = autoHeight_;
}

void SetTextAutoHeightCommand::undo(Document &document) {
    if (!oldAutoHeight_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->type() != LayerType::Text) {
        return;
    }
    static_cast<TextContent *>(layer->content.get())->autoHeight = *oldAutoHeight_;
}

bool SetTextAutoHeightCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetTextAutoHeight) {
        return false;
    }
    const auto &typed = static_cast<const SetTextAutoHeightCommand &>(other);
    if (typed.layerId_ != layerId_) {
        return false;
    }
    autoHeight_ = typed.autoHeight_;
    return true;
}

CommandKind SetTextAutoHeightCommand::kind() const {
    return CommandKind::SetTextAutoHeight;
}

std::string SetTextAutoHeightCommand::describe() const {
    return "Set Text Auto Height";
}

}  // namespace motion
