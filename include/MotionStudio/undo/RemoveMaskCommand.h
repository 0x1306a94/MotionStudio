#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Removes the path mask at an index. execute() takes ownership and undo()
// re-inserts it at the original index.
class RemoveMaskCommand : public Command {
  public:
    // layerId: target layer.
    // index: position within the layer's mask list.
    RemoveMaskCommand(EntityId layerId, int index);

    void execute(Document &document) override;
    void undo(Document &document) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    int index_ = -1;
    std::optional<Mask> mask_;
};

}  // namespace motion
