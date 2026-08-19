#include "MotionStudio/undo/SetStrokeMiterLimitCommand.h"

#include "FindStrokeStyle.h"

namespace motion {

SetStrokeMiterLimitCommand::SetStrokeMiterLimitCommand(EntityId layerId, int index, float miterLimit)
    : layerId_(layerId)
    , index_(index)
    , miterLimit_(miterLimit) {
}

void SetStrokeMiterLimitCommand::execute(Document &document) {
    StrokeStyle *stroke = FindStroke(document, layerId_, index_);
    if (stroke == nullptr) {
        return;
    }
    if (!oldMiterLimit_) {
        oldMiterLimit_ = stroke->miterLimit;
    }
    stroke->miterLimit = miterLimit_;
}

void SetStrokeMiterLimitCommand::undo(Document &document) {
    if (!oldMiterLimit_) {
        return;
    }
    StrokeStyle *stroke = FindStroke(document, layerId_, index_);
    if (stroke != nullptr) {
        stroke->miterLimit = *oldMiterLimit_;
    }
}

bool SetStrokeMiterLimitCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetStrokeMiterLimit) {
        return false;
    }
    const auto &typed = static_cast<const SetStrokeMiterLimitCommand &>(other);
    if (typed.layerId_ != layerId_ || typed.index_ != index_) {
        return false;
    }
    miterLimit_ = typed.miterLimit_;
    return true;
}

CommandKind SetStrokeMiterLimitCommand::kind() const {
    return CommandKind::SetStrokeMiterLimit;
}

std::string SetStrokeMiterLimitCommand::describe() const {
    return "Set Stroke Miter Limit";
}

}  // namespace motion
