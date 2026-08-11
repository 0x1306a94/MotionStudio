#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/GradientPaint.h"
#include "MotionStudio/model/ShaderUniformValues.h"
#include "MotionStudio/model/StylePaintMode.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets Fill/Stroke paintMode (kind). Does not clear inactive color/gradient/shader
// fields. Lazy-inits default gradient or binds a shader when the target path is
// not yet usable.
class SetStylePaintModeCommand : public Command {
  public:
    // layerId: target layer.
    // styleIndex: index into layer.styles.
    // mode: desired paint kind.
    // shaderId: used when mode is Shader and the style has no valid shader yet;
    //           ignored for Color/Gradient. When rebinding to a different shader,
    //           pass the new id (resets uniformValues via BindShaderPaint).
    SetStylePaintModeCommand(EntityId layerId, int styleIndex, StylePaintMode mode,
                             EntityId shaderId = {});

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    struct PaintSnapshot {
        StylePaintMode paintMode = StylePaintMode::Color;
        EntityId shaderId = {};
        ShaderUniformValues uniformValues;
        GradientPaint gradient;
    };

    EntityId layerId_ = {};
    int styleIndex_ = -1;
    StylePaintMode mode_ = StylePaintMode::Color;
    EntityId shaderId_ = {};
    std::optional<PaintSnapshot> oldPaint_;
};

}  // namespace motion
