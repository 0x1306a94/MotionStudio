#include "MotionStudio/undo/MoveLayerCommand.h"

#include "MotionStudio/model/Document.h"

namespace motion {

MoveLayerCommand::MoveLayerCommand(EntityId compositionId, int fromIndex, int toIndex)
    : compositionId_(compositionId)
    , fromIndex_(fromIndex)
    , toIndex_(toIndex) {
}

void MoveLayerCommand::execute(Document &document) {
    document.moveLayer(compositionId_, fromIndex_, toIndex_);
}

void MoveLayerCommand::undo(Document &document) {
    document.moveLayer(compositionId_, toIndex_, fromIndex_);
}

bool MoveLayerCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::MoveLayer) {
        return false;
    }
    const auto &typed = static_cast<const MoveLayerCommand &>(other);
    if (typed.compositionId_ != compositionId_) {
        return false;
    }
    if (typed.fromIndex_ != toIndex_) {
        return false;  // Only merge consecutive drags
    }
    toIndex_ = typed.toIndex_;
    return true;
}

CommandKind MoveLayerCommand::kind() const {
    return CommandKind::MoveLayer;
}

std::string MoveLayerCommand::describe() const {
    return "Move Layer";
}

}  // namespace motion
