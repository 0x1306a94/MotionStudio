#pragma once

#include <optional>
#include <string>

#include "MotionStudio/model/PropertyPath.h"
#include "MotionStudio/undo/Command.h"
#include "MotionStudio/undo/KeyframeData.h"

namespace motion {

// Adds a keyframe. If a keyframe at the same time already exists, records the
// replaced one and restores it on undo.
class AddKeyframeCommand : public Command {
public:
    // property: path to the animatable property.
    // keyframe: the keyframe to add.
    AddKeyframeCommand(PropertyPath property, KeyframeData keyframe);

    void execute(Document& document) override;
    void undo(Document& document) override;
    CommandKind kind() const override;
    std::string describe() const override;

private:
    PropertyPath property_;
    KeyframeData keyframe_;
    std::optional<KeyframeData> replaced_;
    bool captured_ = false;
};

}  // namespace motion
