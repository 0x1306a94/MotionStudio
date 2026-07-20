#include "MotionStudio/undo/SetStaticValueCommand.h"

#include <utility>

#include "CommandHelpers.h"
#include "MotionStudio/model/Document.h"

namespace motion {

SetStaticValueCommand::SetStaticValueCommand(PropertyPath property, PropertyValue value)
    : property_(std::move(property))
    , value_(std::move(value)) {
}

void SetStaticValueCommand::execute(Document &document) {
    AnimatableBase *target = ResolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    PropertyValue oldValue;
    if (!ApplyStaticValueAny(target, value_, oldValue)) {
        return;
    }
    if (!captured_) {
        oldValue_ = std::move(oldValue);
        captured_ = true;
    }
}

void SetStaticValueCommand::undo(Document &document) {
    if (!captured_) {
        return;
    }
    AnimatableBase *target = ResolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    PropertyValue discarded;
    ApplyStaticValueAny(target, *oldValue_, discarded);
}

bool SetStaticValueCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetStaticValue) {
        return false;
    }
    const auto &typed = static_cast<const SetStaticValueCommand &>(other);
    if (typed.property_ != property_) {
        return false;
    }
    value_ = typed.value_;  // Preserve oldValue_, absorb final value
    return true;
}

CommandKind SetStaticValueCommand::kind() const {
    return CommandKind::SetStaticValue;
}

std::string SetStaticValueCommand::describe() const {
    return "Set Value";
}

}  // namespace motion
