#include "MotionStudio/undo/AddKeyframeCommand.h"

#include <utility>

#include "CommandHelpers.h"
#include "MotionStudio/model/Document.h"

namespace motion {

AddKeyframeCommand::AddKeyframeCommand(PropertyPath property, KeyframeData keyframe)
    : property_(std::move(property))
    , keyframe_(std::move(keyframe)) {
}

void AddKeyframeCommand::execute(Document &document) {
    AnimatableBase *target = ResolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    if (!captured_) {
        replaced_ = TakeKeyframeAny(target, KeyframeTime(keyframe_));
        captured_ = true;
    }
    AddKeyframeAny(target, keyframe_);
}

void AddKeyframeCommand::undo(Document &document) {
    if (!captured_) {
        return;
    }
    AnimatableBase *target = ResolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    TakeKeyframeAny(target, KeyframeTime(keyframe_));
    if (replaced_) {
        AddKeyframeAny(target, *replaced_);
    }
}

bool AddKeyframeCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::AddKeyframe) {
        return false;
    }
    const auto &typed = static_cast<const AddKeyframeCommand &>(other);
    if (typed.property_ != property_ || KeyframeTime(typed.keyframe_) != KeyframeTime(keyframe_)) {
        return false;
    }
    keyframe_ = typed.keyframe_;  // Preserve replaced_, absorb final value.
    return true;
}

CommandKind AddKeyframeCommand::kind() const {
    return CommandKind::AddKeyframe;
}

std::string AddKeyframeCommand::describe() const {
    return "Add Keyframe";
}

}  // namespace motion
