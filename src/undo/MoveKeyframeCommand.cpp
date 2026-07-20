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
    AnimatableBase* target = resolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    if (!captured_) {
        overwritten_ = takeKeyframeAny(target, newTime_);
        captured_ = true;
    }
    std::optional<KeyframeData> moved = takeKeyframeAny(target, oldTime_);
    if (!moved) {
        return;
    }
    setKeyframeTime(*moved, newTime_);
    addKeyframeAny(target, *moved);
}

void MoveKeyframeCommand::undo(Document& document) {
    if (!captured_) {
        return;
    }
    AnimatableBase* target = resolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    std::optional<KeyframeData> moved = takeKeyframeAny(target, newTime_);
    if (overwritten_) {
        addKeyframeAny(target, *overwritten_);
    }
    if (moved) {
        setKeyframeTime(*moved, oldTime_);
        addKeyframeAny(target, *moved);
    }
}

bool MoveKeyframeCommand::mergeWith(const Command& other) {
    const auto* typed = dynamic_cast<const MoveKeyframeCommand*>(&other);
    if (!typed || typed->property_ != property_) {
        return false;
    }
    if (typed->oldTime_ != newTime_) {
        return false;  // 仅合并连续拖动
    }
    newTime_ = typed->newTime_;
    return true;
}

std::string MoveKeyframeCommand::describe() const {
    return "Move Keyframe";
}

}  // namespace motion
