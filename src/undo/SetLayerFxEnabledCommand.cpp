#include "MotionStudio/undo/SetLayerFxEnabledCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

SetLayerFxEnabledCommand::SetLayerFxEnabledCommand(EntityId layerId, int index, bool enabled)
    : layerId_(layerId)
    , index_(index)
    , enabled_(enabled) {
}

void SetLayerFxEnabledCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || index_ < 0 ||
        static_cast<size_t>(index_) >= layer->layerStyles.size()) {
        return;
    }
    LayerFx &style = *layer->layerStyles[static_cast<size_t>(index_)];
    if (!oldEnabled_) {
        oldEnabled_ = style.enabled;
    }
    style.enabled = enabled_;
}

void SetLayerFxEnabledCommand::undo(Document &document) {
    if (!oldEnabled_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || index_ < 0 ||
        static_cast<size_t>(index_) >= layer->layerStyles.size()) {
        return;
    }
    layer->layerStyles[static_cast<size_t>(index_)]->enabled = *oldEnabled_;
}

bool SetLayerFxEnabledCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetLayerFxEnabled) {
        return false;
    }
    const auto &typed = static_cast<const SetLayerFxEnabledCommand &>(other);
    if (typed.layerId_ != layerId_ || typed.index_ != index_) {
        return false;
    }
    enabled_ = typed.enabled_;
    return true;
}

CommandKind SetLayerFxEnabledCommand::kind() const {
    return CommandKind::SetLayerFxEnabled;
}

std::string SetLayerFxEnabledCommand::describe() const {
    return "Set Layer Style Enabled";
}

}  // namespace motion
