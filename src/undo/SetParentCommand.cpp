#include "MotionStudio/undo/SetParentCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

SetParentCommand::SetParentCommand(EntityId layerId, EntityId newParentId)
    : layerId_(layerId)
    , newParentId_(newParentId) {
}

void SetParentCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    if (!oldParentId_) {
        oldParentId_ = layer->parentId;
    }
    if (!layer->setParent(newParentId_, document)) {
        return;
    }
    applied_ = true;
}

void SetParentCommand::undo(Document &document) {
    if (!applied_ || !oldParentId_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    layer->setParent(*oldParentId_, document);
}

CommandKind SetParentCommand::kind() const {
    return CommandKind::SetParent;
}

std::string SetParentCommand::describe() const {
    return "Set Parent";
}

}  // namespace motion
