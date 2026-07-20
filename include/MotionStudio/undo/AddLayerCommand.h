#pragma once

#include <memory>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Adds a layer. execute() takes over the unique_ptr ownership and undo()
// moves it back. Redo re-inserts at the recorded index.
class AddLayerCommand : public Command {
public:
    // compositionId: host composition for the new layer.
    // layer: takes ownership of the layer to add.
    // index: insertion position (-1 to append).
    AddLayerCommand(EntityId compositionId, std::unique_ptr<Layer> layer, int index = -1);

    void execute(Document& document) override;
    void undo(Document& document) override;
    CommandKind kind() const override;
    std::string describe() const override;

private:
    EntityId compositionId_;
    EntityId layerId_;
    int index_;
    std::unique_ptr<Layer> layer_;
};

}  // namespace motion
