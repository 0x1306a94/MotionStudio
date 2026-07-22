#include "MotionStudio/undo/SetCompositionBackgroundColorCommand.h"

#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"

namespace motion {

SetCompositionBackgroundColorCommand::SetCompositionBackgroundColorCommand(EntityId compositionId,
                                                                           Color color)
    : compositionId_(compositionId)
    , color_(color) {
}

void SetCompositionBackgroundColorCommand::execute(Document &document) {
    Composition *composition = document.entityIndex().findComposition(compositionId_);
    if (composition == nullptr) {
        return;
    }
    if (!oldColor_) {
        oldColor_ = composition->backgroundColor;
    }
    composition->backgroundColor = color_;
}

void SetCompositionBackgroundColorCommand::undo(Document &document) {
    if (!oldColor_) {
        return;
    }
    Composition *composition = document.entityIndex().findComposition(compositionId_);
    if (composition != nullptr) {
        composition->backgroundColor = *oldColor_;
    }
}

bool SetCompositionBackgroundColorCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetCompositionBackgroundColor) {
        return false;
    }
    const auto &typed = static_cast<const SetCompositionBackgroundColorCommand &>(other);
    if (typed.compositionId_ != compositionId_) {
        return false;
    }
    color_ = typed.color_;
    return true;
}

CommandKind SetCompositionBackgroundColorCommand::kind() const {
    return CommandKind::SetCompositionBackgroundColor;
}

std::string SetCompositionBackgroundColorCommand::describe() const {
    return "Set Composition Background";
}

}  // namespace motion
