#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/Time.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/PropertyPath.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets spatial in/out tangents on a Vec2 keyframe. No-op when the property is
// not Animatable<Vec2> or no keyframe exists at time. Passing nullopt clears
// that handle (segment falls back to a straight line).
class SetSpatialTangentsCommand : public Command {
  public:
    SetSpatialTangentsCommand(PropertyPath property, FrameTime time,
                              std::optional<Vec2> spatialIn, std::optional<Vec2> spatialOut);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    PropertyPath property_;
    FrameTime time_;
    std::optional<Vec2> spatialIn_;
    std::optional<Vec2> spatialOut_;
    std::optional<Vec2> oldSpatialIn_;
    std::optional<Vec2> oldSpatialOut_;
    bool captured_ = false;
    bool found_ = false;
};

}  // namespace motion
