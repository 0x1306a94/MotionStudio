#include "MotionStudio/undo/AddKeyframeCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"
#include "CommandHelpers.h"

namespace motion {

AddKeyframeCommand::AddKeyframeCommand(PropertyPath property, KeyframeData keyframe)
    : property_(std::move(property)), keyframe_(std::move(keyframe)) {}

void AddKeyframeCommand::execute(Document& document) {
    AnimatableBase* target = resolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    if (!captured_) {
        replaced_ = takeKeyframeAny(target, keyframeTime(keyframe_));
        captured_ = true;
    }
    addKeyframeAny(target, keyframe_);
}

void AddKeyframeCommand::undo(Document& document) {
    if (!captured_) {
        return;
    }
    AnimatableBase* target = resolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    takeKeyframeAny(target, keyframeTime(keyframe_));  // 移除本次加入的
    if (replaced_) {
        addKeyframeAny(target, *replaced_);
    }
}

CommandKind AddKeyframeCommand::kind() const {
    return CommandKind::AddKeyframe;
}

std::string AddKeyframeCommand::describe() const {
    return "Add Keyframe";
}

}  // namespace motion
