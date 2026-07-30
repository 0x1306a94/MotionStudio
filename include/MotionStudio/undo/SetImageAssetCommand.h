#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Binds or clears ImageContent::assetId. assetId invalid = unbind.
class SetImageAssetCommand : public Command {
  public:
    SetImageAssetCommand(EntityId layerId, EntityId assetId);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    EntityId assetId_;
    std::optional<EntityId> oldAssetId_;
};

}  // namespace motion
