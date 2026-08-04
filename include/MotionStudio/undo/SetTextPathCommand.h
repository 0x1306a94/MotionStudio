#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets TextPath enabled flag, path layer id, and static bools.
// firstMargin / lastMargin use property commands. Consecutive sets merge.
class SetTextPathCommand : public Command {
  public:
    // layerId: text layer.
    // enabled: whether path layout is active.
    // pathLayerId: path source layer (invalid clears the binding).
    // reversed / perpendicular / forceAlignment: static TextPath flags.
    SetTextPathCommand(EntityId layerId, bool enabled, EntityId pathLayerId, bool reversed,
                       bool perpendicular, bool forceAlignment);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    bool enabled_ = false;
    EntityId pathLayerId_ = {};
    bool reversed_ = false;
    bool perpendicular_ = true;
    bool forceAlignment_ = false;
    std::optional<bool> oldEnabled_;
    std::optional<EntityId> oldPathLayerId_;
    std::optional<bool> oldReversed_;
    std::optional<bool> oldPerpendicular_;
    std::optional<bool> oldForceAlignment_;
};

}  // namespace motion
