#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets Follow Path enabled flag, path layer id, and orient-along-path.
// pathOffset / orientOffset use property commands. Consecutive sets merge.
class SetFollowPathCommand : public Command {
  public:
    // layerId: follower layer.
    // enabled: whether the constraint is active.
    // pathLayerId: path source layer (invalid clears the binding).
    // orientAlongPath: whether rotation follows the path tangent.
    SetFollowPathCommand(EntityId layerId, bool enabled, EntityId pathLayerId,
                         bool orientAlongPath);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    bool enabled_ = false;
    EntityId pathLayerId_ = {};
    bool orientAlongPath_ = true;
    std::optional<bool> oldEnabled_;
    std::optional<EntityId> oldPathLayerId_;
    std::optional<bool> oldOrientAlongPath_;
};

}  // namespace motion
