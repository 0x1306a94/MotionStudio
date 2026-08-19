#pragma once

#include <optional>
#include <string>
#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Replaces the static dash interval array on a stroke style.
class SetStrokeDashPatternCommand : public Command {
  public:
    // layerId: target layer.
    // index: style list index of the stroke.
    // dashes: raw on/off lengths; Dashed + invalid pattern is a no-op.
    SetStrokeDashPatternCommand(EntityId layerId, int index, std::vector<float> dashes);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    int index_ = -1;
    std::vector<float> dashes_;
    std::optional<std::vector<float>> oldDashes_;
};

}  // namespace motion
