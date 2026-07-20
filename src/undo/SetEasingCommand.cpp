#include "MotionStudio/undo/SetEasingCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"
#include "CommandHelpers.h"

namespace motion {

SetEasingCommand::SetEasingCommand(PropertyPath property, FrameTime time, Easing easing)
    : property_(std::move(property)), time_(time), easing_(easing) {}

void SetEasingCommand::execute(Document& document) {
    AnimatableBase* target = ResolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    if (!captured_) {
        Easing oldEasing;
        found_ = ApplyEasingAny(target, time_, easing_, &oldEasing);
        if (found_) {
            oldEasing_ = oldEasing;
        }
        captured_ = true;
    } else if (found_) {
        ApplyEasingAny(target, time_, easing_, nullptr);
    }
}

void SetEasingCommand::undo(Document& document) {
    if (!captured_ || !found_) {
        return;
    }
    AnimatableBase* target = ResolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    ApplyEasingAny(target, time_, *oldEasing_, nullptr);
}

bool SetEasingCommand::mergeWith(const Command& other) {
    if (other.kind() != CommandKind::SetEasing) {
        return false;
    }
    const auto& typed = static_cast<const SetEasingCommand&>(other);
    if (typed.property_ != property_ || typed.time_ != time_) {
        return false;
    }
    easing_ = typed.easing_;
    return true;
}

CommandKind SetEasingCommand::kind() const {
    return CommandKind::SetEasing;
}

std::string SetEasingCommand::describe() const {
    return "Set Easing";
}

}  // namespace motion
