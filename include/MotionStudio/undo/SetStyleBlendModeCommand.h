#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets a fill style's blend mode. Captures the previous value on first
// execution so undo restores it. Consecutive sets on the same style merge.
class SetStyleBlendModeCommand : public Command {
  public:
    // layerId: target layer.
    // index: position of the fill within the layer's style list.
    // blendMode: desired blend mode.
    SetStyleBlendModeCommand(EntityId layerId, int index, BlendMode blendMode);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_;
    int index_ = -1;
    BlendMode blendMode_ = BlendMode::Normal;
    std::optional<BlendMode> oldBlendMode_;
};

}  // namespace motion
