#include "MotionStudio/undo/SetTextSizeCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/TextContent.h"

namespace motion {

SetTextSizeCommand::SetTextSizeCommand(EntityId layerId, Vec2 size)
    : layerId_(layerId)
    , size_(size) {
}

void SetTextSizeCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->type() != LayerType::Text) {
        return;
    }
    auto *content = static_cast<TextContent *>(layer->content.get());
    if (!oldSize_) {
        oldSize_ = content->size;
    }
    content->size = size_;
}

void SetTextSizeCommand::undo(Document &document) {
    if (!oldSize_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || layer->type() != LayerType::Text) {
        return;
    }
    static_cast<TextContent *>(layer->content.get())->size = *oldSize_;
}

bool SetTextSizeCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetTextSize) {
        return false;
    }
    const auto &typed = static_cast<const SetTextSizeCommand &>(other);
    if (typed.layerId_ != layerId_) {
        return false;
    }
    size_ = typed.size_;
    return true;
}

CommandKind SetTextSizeCommand::kind() const {
    return CommandKind::SetTextSize;
}

std::string SetTextSizeCommand::describe() const {
    return "Set Text Size";
}

}  // namespace motion
