#pragma once

#include <optional>
#include <string>

#include "MotionStudio/animation/Easing.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/model/PropertyPath.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// 改关键帧缓动。目标帧不存在则为空操作。
class SetEasingCommand : public Command {
public:
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
