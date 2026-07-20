#include "MotionStudio/undo/RemoveKeyframeCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"
#include "CommandHelpers.h"

namespace motion {

RemoveKeyframeCommand::RemoveKeyframeCommand(PropertyPath property, FrameTime time)
    : property_(std::move(property)), time_(time) {}

void RemoveKeyframeCommand::execute(Document& document) {
    AnimatableBase* target = resolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    if (!captured_) {
        removed_ = takeKeyframeAny(target, time_);
        captured_ = true;
    } else {
        takeKeyframeAny(target, time_);  // redo：再次移除
    }
}

void RemoveKeyframeCommand::undo(Document& document) {
    if (!captured_ || !removed_) {
        return;
    }
    AnimatableBase* target = resolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    addKeyframeAny(target, *removed_);
}

std::string RemoveKeyframeCommand::describe() const {
    return "Remove Keyframe";
}

}  // namespace motion
