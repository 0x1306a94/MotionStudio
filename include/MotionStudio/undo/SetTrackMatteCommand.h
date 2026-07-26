#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/TrackMatteType.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets track matte source and type on a layer. Consecutive sets merge.
class SetTrackMatteCommand : public Command {
  public:
    // layerId: layer receiving the track matte.
    // matteLayerId: source layer id (invalid clears the source).
    // type: track matte mode; None clears the matte.
    SetTrackMatteCommand(EntityId layerId, EntityId matteLayerId, TrackMatteType type);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    EntityId matteLayerId_ = {};
    TrackMatteType type_ = TrackMatteType::None;
    std::optional<EntityId> oldMatteLayerId_;
    std::optional<TrackMatteType> oldType_;
};

}  // namespace motion
