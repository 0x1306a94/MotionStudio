#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/LineJoin.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets a stroke style's line join. Consecutive sets on the same style merge.
class SetStrokeJoinCommand : public Command {
  public:
    // layerId: target layer.
    // index: style list index of the stroke.
    // join: desired line join.
    SetStrokeJoinCommand(EntityId layerId, int index, LineJoin join);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    int index_ = -1;
    LineJoin join_ = LineJoin::Miter;
    std::optional<LineJoin> oldJoin_;
};

}  // namespace motion
