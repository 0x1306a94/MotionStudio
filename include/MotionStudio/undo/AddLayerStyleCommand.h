#pragma once

#include <memory>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Adds a fill or stroke to a layer. Fills insert before the first stroke;
// strokes append. The command owns the style between undo() and a subsequent
// redo so the same identity is re-inserted.
class AddLayerStyleCommand : public Command {
  public:
    // layerId: target layer receiving the style.
    // style: the style to add; the command takes ownership.
    AddLayerStyleCommand(EntityId layerId, std::unique_ptr<LayerStyle> style);

    void execute(Document &document) override;
    void undo(Document &document) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    EntityId styleId_;
    std::unique_ptr<LayerStyle> style_;
};

}  // namespace motion
