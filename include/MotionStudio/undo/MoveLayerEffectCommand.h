#pragma once

#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Reorders an effect within the layer's effect list. Out-of-range or
// identical indices are no-ops. Consecutive moves on the same layer merge.
class MoveLayerEffectCommand : public Command {
  public:
    // layerId: target layer.
    // fromIndex / toIndex: positions within Layer::effects.
    MoveLayerEffectCommand(EntityId layerId, int fromIndex, int toIndex);

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
