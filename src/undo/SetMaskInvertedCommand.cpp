#include "MotionStudio/undo/SetMaskInvertedCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"

namespace motion {

SetMaskInvertedCommand::SetMaskInvertedCommand(EntityId layerId, int index, bool inverted)
    : layerId_(layerId)
    , index_(index)
    , inverted_(inverted) {
}

void SetMaskInvertedCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || index_ < 0 ||
        static_cast<size_t>(index_) >= layer->masks.size()) {
        return;
    }
    Mask &mask = layer->masks[static_cast<size_t>(index_)];
    if (!oldInverted_) {
        oldInverted_ = mask.inverted;
    }
    mask.inverted = inverted_;
}

void SetMaskInvertedCommand::undo(Document &document) {
    if (!oldInverted_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || index_ < 0 ||
        static_cast<size_t>(index_) >= layer->masks.size()) {
        return;
    }
    layer->masks[static_cast<size_t>(index_)].inverted = *oldInverted_;
}

bool SetMaskInvertedCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetMaskInverted) {
        return false;
    }
    const auto &typed = static_cast<const SetMaskInvertedCommand &>(other);
    if (typed.layerId_ != layerId_ || typed.index_ != index_) {
        return false;
    }
    inverted_ = typed.inverted_;
    return true;
}

CommandKind SetMaskInvertedCommand::kind() const {
    return CommandKind::SetMaskInverted;
}

std::string SetMaskInvertedCommand::describe() const {
    return "Set Mask Inverted";
}

}  // namespace motion
