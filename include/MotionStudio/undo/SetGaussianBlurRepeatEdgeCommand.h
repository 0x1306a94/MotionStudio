#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets repeatEdgePixels on a GaussianBlur effect. Other effect types are
// no-ops. Consecutive sets on the same effect merge.
class SetGaussianBlurRepeatEdgeCommand : public Command {
  public:
    // layerId: target layer.
    // index: position within the layer's effect list.
    // repeatEdgePixels: desired edge-repeat flag.
    SetGaussianBlurRepeatEdgeCommand(EntityId layerId, int index, bool repeatEdgePixels);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    int index_ = -1;
    bool repeatEdgePixels_ = false;
    std::optional<bool> oldRepeatEdgePixels_;
};

}  // namespace motion
