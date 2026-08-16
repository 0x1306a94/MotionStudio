#include "MotionStudio/undo/SetLayerEffectEnabledCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

SetLayerEffectEnabledCommand::SetLayerEffectEnabledCommand(EntityId layerId, int index,
                                                           bool enabled)
    : layerId_(layerId)
    , index_(index)
    , enabled_(enabled) {
}

void SetLayerEffectEnabledCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || index_ < 0 ||
        static_cast<size_t>(index_) >= layer->effects.size()) {
        return;
    }
    LayerEffect &effect = *layer->effects[static_cast<size_t>(index_)];
    if (!oldEnabled_) {
        oldEnabled_ = effect.enabled;
    }
    effect.enabled = enabled_;
}

void SetLayerEffectEnabledCommand::undo(Document &document) {
    if (!oldEnabled_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || index_ < 0 ||
        static_cast<size_t>(index_) >= layer->effects.size()) {
        return;
    }
    layer->effects[static_cast<size_t>(index_)]->enabled = *oldEnabled_;
}

bool SetLayerEffectEnabledCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetLayerEffectEnabled) {
        return false;
    }
    const auto &typed = static_cast<const SetLayerEffectEnabledCommand &>(other);
    if (typed.layerId_ != layerId_ || typed.index_ != index_) {
        return false;
    }
    enabled_ = typed.enabled_;
    return true;
}

CommandKind SetLayerEffectEnabledCommand::kind() const {
    return CommandKind::SetLayerEffectEnabled;
}

std::string SetLayerEffectEnabledCommand::describe() const {
    return "Set Effect Enabled";
}

}  // namespace motion
