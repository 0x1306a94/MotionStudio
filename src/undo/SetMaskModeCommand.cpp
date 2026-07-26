#include "MotionStudio/undo/SetMaskModeCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

SetMaskModeCommand::SetMaskModeCommand(EntityId layerId, int index, MaskMode mode)
    : layerId_(layerId)
    , index_(index)
    , mode_(mode) {
}

void SetMaskModeCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || index_ < 0 ||
        static_cast<size_t>(index_) >= layer->masks.size()) {
        return;
    }
    Mask &mask = layer->masks[static_cast<size_t>(index_)];
    if (!oldMode_) {
        oldMode_ = mask.mode;
    }
    mask.mode = mode_;
}

void SetMaskModeCommand::undo(Document &document) {
    if (!oldMode_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || index_ < 0 ||
        static_cast<size_t>(index_) >= layer->masks.size()) {
        return;
    }
    layer->masks[static_cast<size_t>(index_)].mode = *oldMode_;
}

bool SetMaskModeCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetMaskMode) {
        return false;
    }
    const auto &typed = static_cast<const SetMaskModeCommand &>(other);
    if (typed.layerId_ != layerId_ || typed.index_ != index_) {
        return false;
    }
    mode_ = typed.mode_;
    return true;
}

CommandKind SetMaskModeCommand::kind() const {
    return CommandKind::SetMaskMode;
}

std::string SetMaskModeCommand::describe() const {
    return "Set Mask Mode";
}

}  // namespace motion
