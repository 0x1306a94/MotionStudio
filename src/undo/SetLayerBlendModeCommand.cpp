#include "MotionStudio/undo/SetLayerBlendModeCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

SetLayerBlendModeCommand::SetLayerBlendModeCommand(EntityId layerId, BlendMode blendMode)
    : layerId_(layerId)
    , blendMode_(blendMode) {
}

void SetLayerBlendModeCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr) {
        return;
    }
    if (!oldBlendMode_) {
        oldBlendMode_ = layer->blendMode;
    }
    layer->blendMode = blendMode_;
}

void SetLayerBlendModeCommand::undo(Document &document) {
    if (!oldBlendMode_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer != nullptr) {
        layer->blendMode = *oldBlendMode_;
    }
}

bool SetLayerBlendModeCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetLayerBlendMode) {
        return false;
    }
    const auto &typed = static_cast<const SetLayerBlendModeCommand &>(other);
    if (typed.layerId_ != layerId_) {
        return false;
    }
    blendMode_ = typed.blendMode_;
    return true;
}

CommandKind SetLayerBlendModeCommand::kind() const {
    return CommandKind::SetLayerBlendMode;
}

std::string SetLayerBlendModeCommand::describe() const {
    return "Set Layer Blend Mode";
}

}  // namespace motion
