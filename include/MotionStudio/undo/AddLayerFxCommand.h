#pragma once

#include <memory>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/LayerFx.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Appends a layer style. The command owns the style between undo() and a
// subsequent redo so the same identity is re-inserted.
class AddLayerFxCommand : public Command {
  public:
    // layerId: target layer receiving the style.
    // style: the style to append; the command takes ownership.
    AddLayerFxCommand(EntityId layerId, std::unique_ptr<LayerFx> style);

    void execute(Document &document) override;
    void undo(Document &document) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    EntityId styleId_ = {};
    std::unique_ptr<LayerFx> style_;
};

}  // namespace motion
