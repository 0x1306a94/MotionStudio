#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets enabled on a layer effect. Consecutive sets on the same effect merge.
class SetLayerEffectEnabledCommand : public Command {
  public:
    // layerId: target layer.
    // index: position within the layer's effect list.
    // enabled: desired enabled flag.
    SetLayerEffectEnabledCommand(EntityId layerId, int index, bool enabled);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    int index_ = -1;
    bool enabled_ = true;
    std::optional<bool> oldEnabled_;
};

}  // namespace motion
