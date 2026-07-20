#pragma once

#include <optional>
#include <string>

#include "MotionStudio/model/PropertyPath.h"
#include "MotionStudio/undo/Command.h"
#include "MotionStudio/undo/PropertyValue.h"

namespace motion {

// Sets a static (non-keyframed) property value. Captures the old value on
// first execution; consecutive sets on the same target are merged (e.g.
// dragging a numeric slider).
class SetStaticValueCommand : public Command {
public:
    // property: path to the animatable property to set.
    // value: the new static value.
    SetStaticValueCommand(PropertyPath property, PropertyValue value);

    void execute(Document& document) override;
    void undo(Document& document) override;
    bool mergeWith(const Command& other) override;
    CommandKind kind() const override;
    std::string describe() const override;

private:
    PropertyPath property_;
    PropertyValue value_;
    std::optional<PropertyValue> oldValue_;
    bool captured_ = false;
};

}  // namespace motion
