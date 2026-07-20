#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/Time.h"
#include "MotionStudio/model/PropertyPath.h"
#include "MotionStudio/undo/Command.h"
#include "MotionStudio/undo/KeyframeData.h"

namespace motion {

// 移动关键帧。记录目标帧上被覆盖的关键帧，undo 时一并还原。
// 连续拖拽（other.oldTime == 本命令 newTime）可合并。
class MoveKeyframeCommand : public Command {
public:
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
