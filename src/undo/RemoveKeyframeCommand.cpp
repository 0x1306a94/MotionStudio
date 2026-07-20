#include "MotionStudio/undo/RemoveKeyframeCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"
#include "CommandHelpers.h"

namespace motion {

RemoveKeyframeCommand::RemoveKeyframeCommand(PropertyPath property, FrameTime time)
    : property_(std::move(property)), time_(time) {}

void RemoveKeyframeCommand::execute(Document& document) {
    AnimatableBase* target = ResolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    if (!captured_) {
        removed_ = TakeKeyframeAny(target, time_);
        captured_ = true;
    } else {
        TakeKeyframeAny(target, time_);
    }
}

void RemoveKeyframeCommand::undo(Document& document) {
    if (!captured_ || !removed_) {
        return;
    }
    AnimatableBase* target = ResolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    AddKeyframeAny(target, *removed_);
}

CommandKind RemoveKeyframeCommand::kind() const {
    return CommandKind::RemoveKeyframe;
}

std::string RemoveKeyframeCommand::describe() const {
    return "Remove Keyframe";
}

}  // namespace motion
