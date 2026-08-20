#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/Asset.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Removes a document asset when no image layer references it.
// Disk files under assets/ are left in place, matching ImportImageAssetCommand undo.
class RemoveAssetCommand : public Command {
  public:
    explicit RemoveAssetCommand(EntityId assetId);

    void execute(Document &document) override;
    void undo(Document &document) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId assetId_ = {};
    std::optional<Asset> removedAsset_;
    int index_ = -1;
};

}  // namespace motion
