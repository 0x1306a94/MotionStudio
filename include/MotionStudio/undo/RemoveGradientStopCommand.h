#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/GradientPaint.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

class RemoveGradientStopCommand : public Command {
  public:
    RemoveGradientStopCommand(EntityId layerId, int styleIndex, int stopIndex);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    int styleIndex_ = -1;
    int stopIndex_ = -1;
    std::optional<GradientStop> removedStop_;
};

}  // namespace motion
