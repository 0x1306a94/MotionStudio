#pragma once

#include <optional>
#include <string>
#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/StrokeMode.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets strokeMode. Switching to Dashed with an invalid pattern seeds {8, 8}.
class SetStrokeModeCommand : public Command {
  public:
    // layerId: target layer.
    // index: style list index of the stroke.
    // strokeMode: Solid or Dashed.
    SetStrokeModeCommand(EntityId layerId, int index, StrokeMode strokeMode);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    int index_ = -1;
    StrokeMode strokeMode_ = StrokeMode::Solid;
    std::optional<StrokeMode> oldStrokeMode_;
    std::vector<float> oldDashes_;
};

}  // namespace motion
