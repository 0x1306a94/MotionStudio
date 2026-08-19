#include "MotionStudio/undo/SetStrokeDashPatternCommand.h"

#include "FindStrokeStyle.h"
#include "MotionStudio/model/StrokeDash.h"

namespace motion {

SetStrokeDashPatternCommand::SetStrokeDashPatternCommand(EntityId layerId, int index,
                                                         std::vector<float> dashes)
    : layerId_(layerId)
    , index_(index)
    , dashes_(std::move(dashes)) {
}

void SetStrokeDashPatternCommand::execute(Document &document) {
    StrokeStyle *stroke = FindStroke(document, layerId_, index_);
    if (stroke == nullptr) {
        return;
    }
    if (stroke->strokeMode == StrokeMode::Dashed && NormalizeDashArray(dashes_).empty()) {
        return;
    }
    if (!oldDashes_) {
        oldDashes_ = stroke->dashes;
    }
    stroke->dashes = dashes_;
}

void SetStrokeDashPatternCommand::undo(Document &document) {
    if (!oldDashes_) {
        return;
    }
    StrokeStyle *stroke = FindStroke(document, layerId_, index_);
    if (stroke != nullptr) {
        stroke->dashes = *oldDashes_;
    }
}

bool SetStrokeDashPatternCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetStrokeDashPattern) {
        return false;
    }
    const auto &typed = static_cast<const SetStrokeDashPatternCommand &>(other);
    if (typed.layerId_ != layerId_ || typed.index_ != index_) {
        return false;
    }
    dashes_ = typed.dashes_;
    return true;
}

CommandKind SetStrokeDashPatternCommand::kind() const {
    return CommandKind::SetStrokeDashPattern;
}

std::string SetStrokeDashPatternCommand::describe() const {
    return "Set Stroke Dash Pattern";
}

}  // namespace motion
