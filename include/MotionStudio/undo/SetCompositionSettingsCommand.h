#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

struct CompositionSettings {
    int width = 1920;
    int height = 1080;
    FrameTime duration = 0;
    FrameRate frameRate;
};

// Updates composition dimensions and timing metadata as one undoable unit.
// Consecutive sets on the same composition merge.
class SetCompositionSettingsCommand : public Command {
  public:
    SetCompositionSettingsCommand(EntityId compositionId, CompositionSettings settings);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId compositionId_;
    CompositionSettings settings_;
    std::optional<CompositionSettings> oldSettings_;
};

}  // namespace motion
