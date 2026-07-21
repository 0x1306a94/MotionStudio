#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Toggles a layer's visibility. Captures the previous value on first execution
// so undo restores it. Consecutive sets on the same layer merge.
class SetLayerVisibleCommand : public Command {
  public:
    // layerId: target layer.
    // visible: desired visibility.
    SetLayerVisibleCommand(EntityId layerId, bool visible);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    bool visible_;
    std::optional<bool> oldVisible_;
};

}  // namespace motion
