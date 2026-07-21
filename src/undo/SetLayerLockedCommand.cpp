#include "MotionStudio/undo/SetLayerLockedCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

SetLayerLockedCommand::SetLayerLockedCommand(EntityId layerId, bool locked)
    : layerId_(layerId)
    , locked_(locked) {
}

void SetLayerLockedCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    if (!oldLocked_) {
        oldLocked_ = layer->locked;
    }
    layer->locked = locked_;
}

void SetLayerLockedCommand::undo(Document &document) {
    if (!oldLocked_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer != nullptr) {
        layer->locked = *oldLocked_;
    }
}

bool SetLayerLockedCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetLayerLocked) {
        return false;
    }
    const auto &typed = static_cast<const SetLayerLockedCommand &>(other);
    if (typed.layerId_ != layerId_) {
        return false;
    }
    locked_ = typed.locked_;  // Preserve oldLocked_, absorb final value.
    return true;
}

CommandKind SetLayerLockedCommand::kind() const {
    return CommandKind::SetLayerLocked;
}

std::string SetLayerLockedCommand::describe() const {
    return "Set Layer Lock";
}

}  // namespace motion
