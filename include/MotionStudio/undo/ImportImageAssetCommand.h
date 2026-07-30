#pragma once

#include <string>

#include "MotionStudio/model/Asset.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Adds a document-level asset. Undo removes it from Document::assets only;
// disk files under assets/ are left in place.
class ImportImageAssetCommand : public Command {
  public:
    explicit ImportImageAssetCommand(Asset asset);

    void execute(Document &document) override;
    void undo(Document &document) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    Asset asset_;
    bool inserted_ = false;
};

}  // namespace motion
