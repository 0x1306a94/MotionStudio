#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets a composition corner radius. Captures the previous value on first
// execution so undo restores it. Consecutive sets on the same composition merge.
class SetCompositionCornerRadiusCommand : public Command {
  public:
    SetCompositionCornerRadiusCommand(EntityId compositionId, float cornerRadius);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId compositionId_;
    float cornerRadius_;
    std::optional<float> oldCornerRadius_;
};

}  // namespace motion
