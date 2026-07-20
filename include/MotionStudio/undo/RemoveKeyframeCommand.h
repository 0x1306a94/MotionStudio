#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/Time.h"
#include "MotionStudio/model/PropertyPath.h"
#include "MotionStudio/undo/Command.h"
#include "MotionStudio/undo/KeyframeData.h"

namespace motion {

// 删关键帧。execute 时接管关键帧，undo 时放回。
class RemoveKeyframeCommand : public Command {
public:
    RemoveKeyframeCommand(PropertyPath property, FrameTime time);

    void execute(Document& document) override;
    void undo(Document& document) override;
    CommandKind kind() const override;
    std::string describe() const override;

private:
    PropertyPath property_;
    FrameTime time_;
    std::optional<KeyframeData> removed_;
    bool captured_ = false;
};

}  // namespace motion
