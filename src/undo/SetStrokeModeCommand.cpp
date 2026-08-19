#include "MotionStudio/undo/SetStrokeModeCommand.h"

#include "FindStrokeStyle.h"
#include "MotionStudio/model/StrokeDash.h"

namespace motion {

SetStrokeModeCommand::SetStrokeModeCommand(EntityId layerId, int index, StrokeMode strokeMode)
    : layerId_(layerId)
    , index_(index)
    , strokeMode_(strokeMode) {
}

void SetStrokeModeCommand::execute(Document &document) {
    StrokeStyle *stroke = FindStroke(document, layerId_, index_);
    if (stroke == nullptr) {
        return;
    }
    if (!oldStrokeMode_) {
        oldStrokeMode_ = stroke->strokeMode;
        oldDashes_ = stroke->dashes;
    }
    stroke->strokeMode = strokeMode_;
    if (strokeMode_ == StrokeMode::Dashed && NormalizeDashArray(stroke->dashes).empty()) {
        stroke->dashes = DefaultDashPattern();
    }
}

void SetStrokeModeCommand::undo(Document &document) {
    if (!oldStrokeMode_) {
        return;
    }
    StrokeStyle *stroke = FindStroke(document, layerId_, index_);
    if (stroke != nullptr) {
        stroke->strokeMode = *oldStrokeMode_;
        stroke->dashes = oldDashes_;
    }
}

bool SetStrokeModeCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetStrokeMode) {
        return false;
    }
    const auto &typed = static_cast<const SetStrokeModeCommand &>(other);
    if (typed.layerId_ != layerId_ || typed.index_ != index_) {
        return false;
    }
    strokeMode_ = typed.strokeMode_;
    return true;
}

CommandKind SetStrokeModeCommand::kind() const {
    return CommandKind::SetStrokeMode;
}

std::string SetStrokeModeCommand::describe() const {
    return "Set Stroke Mode";
}

}  // namespace motion
