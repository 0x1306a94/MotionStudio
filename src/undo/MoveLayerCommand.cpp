#include "MotionStudio/undo/MoveLayerCommand.h"

#include "MotionStudio/model/Document.h"

namespace motion {

MoveLayerCommand::MoveLayerCommand(EntityId compositionId, int fromIndex, int toIndex)
    : compositionId_(compositionId), fromIndex_(fromIndex), toIndex_(toIndex) {}

void MoveLayerCommand::execute(Document& document) {
    document.moveLayer(compositionId_, fromIndex_, toIndex_);
}

void MoveLayerCommand::undo(Document& document) {
    document.moveLayer(compositionId_, toIndex_, fromIndex_);
}

bool MoveLayerCommand::mergeWith(const Command& other) {
    const auto* typed = dynamic_cast<const MoveLayerCommand*>(&other);
    if (!typed || typed->compositionId_ != compositionId_) {
        return false;
    }
    if (typed->fromIndex_ != toIndex_) {
        return false;  // 仅合并连续拖动
    }
    toIndex_ = typed->toIndex_;
    return true;
}

std::string MoveLayerCommand::describe() const {
    return "Move Layer";
}

}  // namespace motion
