#include "MotionStudio/undo/MoveKeyframeCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"
#include "CommandHelpers.h"

namespace motion {

MoveKeyframeCommand::MoveKeyframeCommand(PropertyPath property, FrameTime oldTime,
                                         FrameTime newTime)
    : property_(std::move(property)), oldTime_(oldTime), newTime_(newTime) {}

void MoveKeyframeCommand::execute(Document& document) {
    if (oldTime_ == newTime_) {
        return;
    }
    AnimatableBase* target = ResolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    if (!captured_) {
        overwritten_ = TakeKeyframeAny(target, newTime_);
        captured_ = true;
    }
    std::optional<KeyframeData> moved = TakeKeyframeAny(target, oldTime_);
    if (!moved) {
        return;
    }
    SetKeyframeTime(*moved, newTime_);
    AddKeyframeAny(target, *moved);
}

void MoveKeyframeCommand::undo(Document& document) {
    if (!captured_) {
        return;
    }
    AnimatableBase* target = ResolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    std::optional<KeyframeData> moved = TakeKeyframeAny(target, newTime_);
    if (overwritten_) {
        AddKeyframeAny(target, *overwritten_);
    }
    if (moved) {
        SetKeyframeTime(*moved, oldTime_);
        AddKeyframeAny(target, *moved);
    }
}

bool MoveKeyframeCommand::mergeWith(const Command& other) {
    if (other.kind() != CommandKind::MoveKeyframe) {
        return false;
    }
    const auto& typed = static_cast<const MoveKeyframeCommand&>(other);
    if (typed.property_ != property_) {
        return false;
    }
    if (typed.oldTime_ != newTime_) {
        return false;  // Only merge consecutive drags
    }
    newTime_ = typed.newTime_;
    return true;
}

CommandKind MoveKeyframeCommand::kind() const {
    return CommandKind::MoveKeyframe;
}

std::string MoveKeyframeCommand::describe() const {
    return "Move Keyframe";
}

}  // namespace motion
