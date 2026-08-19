#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets a stroke style's static miter limit. Consecutive sets on the same style merge.
class SetStrokeMiterLimitCommand : public Command {
  public:
    // layerId: target layer.
    // index: style list index of the stroke.
    // miterLimit: miter cutoff (typically >= 1).
    SetStrokeMiterLimitCommand(EntityId layerId, int index, float miterLimit);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    int index_ = -1;
    float miterLimit_ = 4.0f;
    std::optional<float> oldMiterLimit_;
};

}  // namespace motion
