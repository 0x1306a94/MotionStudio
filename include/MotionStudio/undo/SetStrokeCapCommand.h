#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/LineCap.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets a stroke style's line cap. Consecutive sets on the same style merge.
class SetStrokeCapCommand : public Command {
  public:
    // layerId: target layer.
    // index: style list index of the stroke.
    // cap: desired line cap.
    SetStrokeCapCommand(EntityId layerId, int index, LineCap cap);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    int index_ = -1;
    LineCap cap_ = LineCap::Butt;
    std::optional<LineCap> oldCap_;
};

}  // namespace motion
