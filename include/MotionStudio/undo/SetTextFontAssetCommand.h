#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Binds or clears TextContent::fontAssetId. Invalid assetId = unbind.
// On successful bind, syncs fontFamily to the asset name (restored on undo).
class SetTextFontAssetCommand : public Command {
  public:
    SetTextFontAssetCommand(EntityId layerId, EntityId assetId);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    EntityId assetId_;
    std::optional<EntityId> oldAssetId_;
    std::optional<std::string> oldFontFamily_;
};

}  // namespace motion
