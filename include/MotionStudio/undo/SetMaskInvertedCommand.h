#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets inverted on a layer mask. Consecutive sets on the same mask merge.
class SetMaskInvertedCommand : public Command {
  public:
    // layerId: target layer.
    // index: position within the layer's mask list.
    // inverted: desired inverted flag.
    SetMaskInvertedCommand(EntityId layerId, int index, bool inverted);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    int index_ = -1;
    bool inverted_ = false;
    std::optional<bool> oldInverted_;
};

}  // namespace motion
