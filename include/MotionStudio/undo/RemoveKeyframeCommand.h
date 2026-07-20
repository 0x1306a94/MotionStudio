#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/Time.h"
#include "MotionStudio/model/PropertyPath.h"
#include "MotionStudio/undo/Command.h"
#include "MotionStudio/undo/KeyframeData.h"

namespace motion {

// Removes a keyframe. execute() captures it and undo() puts it back.
class RemoveKeyframeCommand : public Command {
  public:
    // property: path to the animatable property.
    // time: frame time of the keyframe to remove.
    RemoveKeyframeCommand(PropertyPath property, FrameTime time);

    void execute(Document &document) override;
    void undo(Document &document) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    PropertyPath property_;
    FrameTime time_;
    std::optional<KeyframeData> removed_;
    bool captured_ = false;
};

}  // namespace motion
