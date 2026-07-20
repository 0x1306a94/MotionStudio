#pragma once

#include <memory>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Removes a layer. execute() takes over the unique_ptr ownership and undo()
// moves it back at the original index, restoring the whole subtree and
// keyframes.
class RemoveLayerCommand : public Command {
public:
    // compositionId: host composition of the layer.
    // layerId: id of the layer to remove.
    RemoveLayerCommand(EntityId compositionId, EntityId layerId);

    void execute(Document& document) override;
    void undo(Document& document) override;
    CommandKind kind() const override;
    std::string describe() const override;

private:
    EntityId compositionId_;
    EntityId layerId_;
    int index_ = -1;
    std::unique_ptr<Layer> layer_;
};

}  // namespace motion
