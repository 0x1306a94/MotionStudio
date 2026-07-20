#include "MotionStudio/undo/SetStaticValueCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"
#include "CommandHelpers.h"

namespace motion {

SetStaticValueCommand::SetStaticValueCommand(PropertyPath property, PropertyValue value)
    : property_(std::move(property)), value_(std::move(value)) {}

void SetStaticValueCommand::execute(Document& document) {
    AnimatableBase* target = resolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    PropertyValue oldValue;
    if (!applyStaticValueAny(target, value_, oldValue)) {
        return;
    }
    if (!captured_) {
        oldValue_ = std::move(oldValue);
        captured_ = true;
    }
}

void SetStaticValueCommand::undo(Document& document) {
    if (!captured_) {
        return;
    }
    AnimatableBase* target = resolveAnimatable(document, property_);
    if (!target) {
        return;
    }
    PropertyValue discarded;
    applyStaticValueAny(target, *oldValue_, discarded);
}

bool SetStaticValueCommand::mergeWith(const Command& other) {
    if (other.kind() != CommandKind::SetStaticValue) {
        return false;
    }
    const auto& typed = static_cast<const SetStaticValueCommand&>(other);
    if (typed.property_ != property_) {
        return false;
    }
    value_ = typed.value_;  // 保留 oldValue_，吸收最终值
    return true;
}

CommandKind SetStaticValueCommand::kind() const {
    return CommandKind::SetStaticValue;
}

std::string SetStaticValueCommand::describe() const {
    return "Set Value";
}

}  // namespace motion
