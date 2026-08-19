#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

class SetParentCommand : public Command {
  public:
    // layerId: layer whose parent changes.
    // newParentId: new parent; invalid id clears the parent.
    SetParentCommand(EntityId layerId, EntityId newParentId);

    void execute(Document &document) override;
    void undo(Document &document) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    EntityId newParentId_ = {};
    std::optional<EntityId> oldParentId_ = {};
    bool applied_ = false;
};

}  // namespace motion
