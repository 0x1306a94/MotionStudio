#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets TextContent::autoHeight.
class SetTextAutoHeightCommand : public Command {
  public:
    SetTextAutoHeightCommand(EntityId layerId, bool autoHeight);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    bool autoHeight_ = true;
    std::optional<bool> oldAutoHeight_;
};

}  // namespace motion
