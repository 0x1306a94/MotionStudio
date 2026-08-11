#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

class AddGradientStopCommand : public Command {
  public:
    // insertIndex: clamped to [0, stops.size()].
    AddGradientStopCommand(EntityId layerId, int styleIndex, int insertIndex, Color color,
                           float position);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    int styleIndex_ = -1;
    int insertIndex_ = 0;
    Color color_{0, 0, 0, 1};
    float position_ = 0.5f;
    std::optional<int> appliedIndex_;
};

}  // namespace motion
