#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets a layer style's blend mode. Consecutive sets on the same style merge.
class SetLayerFxBlendModeCommand : public Command {
  public:
    // layerId: target layer.
    // index: position within the layer's style list.
    // blendMode: desired blend mode.
    SetLayerFxBlendModeCommand(EntityId layerId, int index, BlendMode blendMode);

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    int index_ = -1;
    BlendMode blendMode_ = BlendMode::Normal;
    std::optional<BlendMode> oldBlendMode_;
};

}  // namespace motion
