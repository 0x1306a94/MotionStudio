#pragma once

#include <memory>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/LayerEffect.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Removes the layer effect at an index. execute() takes over the unique_ptr
// ownership and undo() re-inserts it at the original index.
class RemoveLayerEffectCommand : public Command {
  public:
    // layerId: target layer.
    // index: position within the layer's effect list.
    RemoveLayerEffectCommand(EntityId layerId, int index);

    void execute(Document &document) override;
    void undo(Document &document) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    EntityId effectId_ = {};
    int index_ = -1;
    std::unique_ptr<LayerEffect> effect_;
};

}  // namespace motion
