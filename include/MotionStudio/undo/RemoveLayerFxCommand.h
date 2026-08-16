#pragma once

#include <memory>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/LayerFx.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Removes the layer style at an index. execute() takes over the unique_ptr
// ownership and undo() re-inserts it at the original index.
class RemoveLayerFxCommand : public Command {
  public:
    // layerId: target layer.
    // index: position within the layer's style list.
    RemoveLayerFxCommand(EntityId layerId, int index);

    void execute(Document &document) override;
    void undo(Document &document) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    EntityId styleId_ = {};
    int index_ = -1;
    std::unique_ptr<LayerFx> style_;
};

}  // namespace motion
