#pragma once

#include <memory>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Appends a new fill style to a layer. The command owns the style between
// undo() and a subsequent redo so the same identity is re-inserted.
class AddFillStyleCommand : public Command {
  public:
    // layerId: target layer receiving the fill.
    explicit AddFillStyleCommand(EntityId layerId);

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
