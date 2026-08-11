#pragma once

#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Reorders a Fill or Stroke within the layer's style list. Cross-type moves
// (Fill ↔ Stroke) and moves that span a type boundary are no-ops.
class MoveLayerStyleCommand : public Command {
  public:
    // layerId: target layer.
    // fromIndex / toIndex: positions within Layer::styles.
    MoveLayerStyleCommand(EntityId layerId, int fromIndex, int toIndex);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    int fromIndex_ = 0;
    int toIndex_ = 0;
};

}  // namespace motion
