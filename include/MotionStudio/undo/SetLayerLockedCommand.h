#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Toggles a layer's locked flag. Captures the previous value on first
// execution so undo restores it. Consecutive sets on the same layer merge.
class SetLayerLockedCommand : public Command {
  public:
    // layerId: target layer.
    // locked: desired locked state.
    SetLayerLockedCommand(EntityId layerId, bool locked);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    bool locked_;
    std::optional<bool> oldLocked_;
};

}  // namespace motion
