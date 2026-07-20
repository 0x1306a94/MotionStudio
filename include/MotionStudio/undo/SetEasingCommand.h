#pragma once

#include <optional>
#include <string>

#include "MotionStudio/animation/Easing.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/model/PropertyPath.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Changes a keyframe's easing curve. No-op when the target keyframe does not
// exist.
class SetEasingCommand : public Command {
public:
    // property: path to the animatable property.
    // time: frame time of the keyframe to modify.
    // easing: the new easing curve.
    SetEasingCommand(PropertyPath property, FrameTime time, Easing easing);

    void execute(Document& document) override;
    void undo(Document& document) override;
    bool mergeWith(const Command& other) override;
    CommandKind kind() const override;
    std::string describe() const override;

private:
    PropertyPath property_;
    FrameTime time_;
    Easing easing_;
    std::optional<Easing> oldEasing_;
    bool captured_ = false;
    bool found_ = false;
};

}  // namespace motion
