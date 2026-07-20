#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/Time.h"
#include "MotionStudio/model/PropertyPath.h"
#include "MotionStudio/undo/Command.h"
#include "MotionStudio/undo/KeyframeData.h"

namespace motion {

// Moves a keyframe to a new time. Records any keyframe overwritten at the
// destination and restores it on undo. Consecutive drags (other.oldTime ==
// this command's newTime) are merged.
class MoveKeyframeCommand : public Command {
public:
    // property: path to the animatable property.
    // oldTime: current frame time of the keyframe.
    // newTime: desired frame time after the move.
    MoveKeyframeCommand(PropertyPath property, FrameTime oldTime, FrameTime newTime);

    void execute(Document& document) override;
    void undo(Document& document) override;
    bool mergeWith(const Command& other) override;
    CommandKind kind() const override;
    std::string describe() const override;

private:
    PropertyPath property_;
    FrameTime oldTime_;
    FrameTime newTime_;
    std::optional<KeyframeData> overwritten_;
    bool captured_ = false;
};

}  // namespace motion
