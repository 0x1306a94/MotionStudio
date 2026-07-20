#pragma once

#include <optional>
#include <string>

#include "MotionStudio/model/PropertyPath.h"
#include "MotionStudio/undo/Command.h"
#include "MotionStudio/undo/KeyframeData.h"

namespace motion {

// 加关键帧。若该帧已有则记录被替换者，undo 时还原。
class AddKeyframeCommand : public Command {
public:
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
