#pragma once

#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Reorders a layer. Consecutive drags (previous toIndex == next fromIndex)
// are merged into a single undo step.
class MoveLayerCommand : public Command {
public:
    // compositionId: host composition of the layer.
    // fromIndex: current position of the layer.
    // toIndex: desired position after the move.
    MoveLayerCommand(EntityId compositionId, int fromIndex, int toIndex);

    void execute(Document& document) override;
    void undo(Document& document) override;
    bool mergeWith(const Command& other) override;
    CommandKind kind() const override;
    std::string describe() const override;

private:
    EntityId compositionId_;
    int fromIndex_;
    int toIndex_;
};

}  // namespace motion
