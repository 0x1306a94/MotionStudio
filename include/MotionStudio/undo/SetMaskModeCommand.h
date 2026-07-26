#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/MaskMode.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets MaskMode on a layer mask. Consecutive sets on the same mask merge.
class SetMaskModeCommand : public Command {
  public:
    // layerId: target layer.
    // index: position within the layer's mask list.
    // mode: desired mask mode.
    SetMaskModeCommand(EntityId layerId, int index, MaskMode mode);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    int index_ = -1;
    MaskMode mode_ = MaskMode::Add;
    std::optional<MaskMode> oldMode_;
};

}  // namespace motion
