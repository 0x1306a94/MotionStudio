#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/StrokePosition.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets a layer stroke style's position. Non-stroke styles are a no-op.
// Consecutive sets on the same style merge.
class SetLayerFxStrokePositionCommand : public Command {
  public:
    // layerId: target layer.
    // index: position within the layer's style list.
    // position: desired stroke position.
    SetLayerFxStrokePositionCommand(EntityId layerId, int index, StrokePosition position);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    int index_ = -1;
    StrokePosition position_ = StrokePosition::Outside;
    std::optional<StrokePosition> oldPosition_;
};

}  // namespace motion
