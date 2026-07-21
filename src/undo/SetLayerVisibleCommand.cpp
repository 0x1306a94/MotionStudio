#include "MotionStudio/undo/SetLayerVisibleCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

SetLayerVisibleCommand::SetLayerVisibleCommand(EntityId layerId, bool visible)
    : layerId_(layerId)
    , visible_(visible) {
}

void SetLayerVisibleCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    if (!oldVisible_) {
        oldVisible_ = layer->visible;
    }
    layer->visible = visible_;
}

void SetLayerVisibleCommand::undo(Document &document) {
    if (!oldVisible_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer != nullptr) {
        layer->visible = *oldVisible_;
    }
}

bool SetLayerVisibleCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetLayerVisible) {
        return false;
    }
    const auto &typed = static_cast<const SetLayerVisibleCommand &>(other);
    if (typed.layerId_ != layerId_) {
        return false;
    }
    visible_ = typed.visible_;  // Preserve oldVisible_, absorb final value.
    return true;
}

CommandKind SetLayerVisibleCommand::kind() const {
    return CommandKind::SetLayerVisible;
}

std::string SetLayerVisibleCommand::describe() const {
    return "Set Layer Visibility";
}

}  // namespace motion
