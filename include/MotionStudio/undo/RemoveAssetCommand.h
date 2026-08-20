#pragma once

#include <optional>
#include <string>
#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/Asset.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Removes a document asset when no image layer references it.
// The backing asset file is deleted and restored on undo.
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
    std::vector<unsigned char> removedFileContents_;
    int index_ = -1;
    bool removedFileExisted_ = false;
};

}  // namespace motion
