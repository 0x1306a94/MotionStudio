#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets TextContent::boxTextMode.
class SetTextBoxTextModeCommand : public Command {
  public:
    SetTextBoxTextModeCommand(EntityId layerId, bool boxTextMode);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    bool boxTextMode_ = false;
    std::optional<bool> oldBoxTextMode_;
};

}  // namespace motion
