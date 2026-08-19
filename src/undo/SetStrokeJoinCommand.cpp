#include "MotionStudio/undo/SetStrokeJoinCommand.h"

#include "FindStrokeStyle.h"

namespace motion {

SetStrokeJoinCommand::SetStrokeJoinCommand(EntityId layerId, int index, LineJoin join)
    : layerId_(layerId)
    , index_(index)
    , join_(join) {
}

void SetStrokeJoinCommand::execute(Document &document) {
    StrokeStyle *stroke = FindStroke(document, layerId_, index_);
    if (stroke == nullptr) {
        return;
    }
    if (!oldJoin_) {
        oldJoin_ = stroke->join;
    }
    stroke->join = join_;
}

void SetStrokeJoinCommand::undo(Document &document) {
    if (!oldJoin_) {
        return;
    }
    StrokeStyle *stroke = FindStroke(document, layerId_, index_);
    if (stroke != nullptr) {
        stroke->join = *oldJoin_;
    }
}

bool SetStrokeJoinCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetStrokeJoin) {
        return false;
    }
    const auto &typed = static_cast<const SetStrokeJoinCommand &>(other);
    if (typed.layerId_ != layerId_ || typed.index_ != index_) {
        return false;
    }
    join_ = typed.join_;
    return true;
}

CommandKind SetStrokeJoinCommand::kind() const {
    return CommandKind::SetStrokeJoin;
}

std::string SetStrokeJoinCommand::describe() const {
    return "Set Stroke Join";
}

}  // namespace motion
