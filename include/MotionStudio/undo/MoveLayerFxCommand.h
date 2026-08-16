#pragma once

#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Reorders a layer style within the layer's style list. Out-of-range or
// identical indices are no-ops. Consecutive moves on the same layer merge.
class MoveLayerFxCommand : public Command {
  public:
    // layerId: target layer.
    // fromIndex / toIndex: positions within Layer::layerStyles.
    MoveLayerFxCommand(EntityId layerId, int fromIndex, int toIndex);

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
