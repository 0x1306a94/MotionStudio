#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets a layer's display name. Captures the previous value on first execution
// so undo restores it. Consecutive sets on the same layer merge.
class SetLayerNameCommand : public Command {
  public:
    // layerId: target layer.
    // name: desired display name.
    SetLayerNameCommand(EntityId layerId, std::string name);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    std::string name_ = {};
    std::optional<std::string> oldName_ = {};
};

}  // namespace motion
