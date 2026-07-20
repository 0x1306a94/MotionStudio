#include "MotionStudio/undo/AddKeyframeCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"
#include "CommandHelpers.h"

namespace motion {

AddKeyframeCommand::AddKeyframeCommand(PropertyPath property, KeyframeData keyframe)
    : property_(std::move(property)), keyframe_(std::move(keyframe)) {}

void AddKeyframeCommand::execute(Document& document) {
    AnimatableBase* target = ResolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    if (!captured_) {
        replaced_ = TakeKeyframeAny(target, KeyframeTime(keyframe_));
        captured_ = true;
    }
    AddKeyframeAny(target, keyframe_);
}

void AddKeyframeCommand::undo(Document& document) {
    if (!captured_) {
        return;
    }
    AnimatableBase* target = ResolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    TakeKeyframeAny(target, KeyframeTime(keyframe_));
    if (replaced_) {
        AddKeyframeAny(target, *replaced_);
    }
}

CommandKind AddKeyframeCommand::kind() const {
    return CommandKind::AddKeyframe;
}

std::string AddKeyframeCommand::describe() const {
    return "Add Keyframe";
}

}  // namespace motion
