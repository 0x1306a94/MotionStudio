#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/StrokePosition.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets a stroke style's position (inside/center/outside). Captures the
// previous value on first execution so undo restores it. Consecutive sets
// on the same style merge.
class SetStrokePositionCommand : public Command {
  public:
    // layerId: target layer.
    // index: position of the stroke within the layer's style list.
    // position: desired stroke position.
    SetStrokePositionCommand(EntityId layerId, int index, StrokePosition position);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    int index_ = -1;
    StrokePosition position_ = StrokePosition::Center;
    std::optional<StrokePosition> oldPosition_;
};

}  // namespace motion
