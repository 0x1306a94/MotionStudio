#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/Asset.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Removes a document asset when no image layer references it.
// The backing asset file is moved to the host trash root and restored on undo.
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
    std::string removedFilePath_;
    std::string trashFilePath_;
    int index_ = -1;
    bool movedFileToTrash_ = false;
};

}  // namespace motion
