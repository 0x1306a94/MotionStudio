#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets a composition background color. Captures the previous color on first
// execution so undo restores it. Consecutive sets on the same composition merge.
class SetCompositionBackgroundColorCommand : public Command {
  public:
    SetCompositionBackgroundColorCommand(EntityId compositionId, Color color);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId compositionId_;
    Color color_;
    std::optional<Color> oldColor_;
};

}  // namespace motion
