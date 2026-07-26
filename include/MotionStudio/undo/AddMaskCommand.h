#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Appends a path mask to a layer. The command owns the mask between undo() and
// a subsequent redo so the same values are re-inserted.
class AddMaskCommand : public Command {
  public:
    // layerId: target layer receiving the mask.
    // mask: mask to append; the command takes ownership of its values.
    AddMaskCommand(EntityId layerId, Mask mask);

    void execute(Document &document) override;
    void undo(Document &document) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    std::optional<Mask> mask_;
    int index_ = -1;
};

}  // namespace motion
