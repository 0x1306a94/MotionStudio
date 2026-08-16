#pragma once

#include <memory>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/LayerEffect.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Appends an effect to a layer. The command owns the effect between undo()
// and a subsequent redo so the same identity is re-inserted.
class AddLayerEffectCommand : public Command {
  public:
    // layerId: target layer receiving the effect.
    // effect: the effect to append; the command takes ownership.
    AddLayerEffectCommand(EntityId layerId, std::unique_ptr<LayerEffect> effect);

    void execute(Document &document) override;
    void undo(Document &document) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId layerId_ = {};
    EntityId effectId_ = {};
    std::unique_ptr<LayerEffect> effect_;
};

}  // namespace motion
