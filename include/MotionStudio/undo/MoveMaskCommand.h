#pragma once

#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Reorders a path mask within a layer's mask list.
class MoveMaskCommand : public Command {
  public:
    // layerId: target layer.
    // fromIndex: current position of the mask.
    // toIndex: desired position after the move.
    MoveMaskCommand(EntityId layerId, int fromIndex, int toIndex);

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
