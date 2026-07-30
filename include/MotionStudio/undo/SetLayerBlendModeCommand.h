#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets Layer::blendMode. Captures the previous value on first execution so
// undo restores it. Consecutive sets on the same layer merge.
class SetLayerBlendModeCommand : public Command {
  public:
    // layerId: target layer.
    // blendMode: desired blend mode.
    SetLayerBlendModeCommand(EntityId layerId, BlendMode blendMode);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    BlendMode blendMode_ = BlendMode::Normal;
    std::optional<BlendMode> oldBlendMode_;
};

}  // namespace motion
