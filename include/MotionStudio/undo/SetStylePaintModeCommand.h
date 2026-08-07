#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/ShaderUniformValues.h"
#include "MotionStudio/model/StylePaintMode.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Sets Fill/Stroke paintMode between Color and Shader.
// Color clears shader fields; Shader binds via FindShader + BindShaderPaint.
class SetStylePaintModeCommand : public Command {
  public:
    // layerId: target layer.
    // styleIndex: index into layer.styles.
    // mode: desired paint mode.
    // shaderId: required when mode is Shader; ignored for Color.
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
    };

    EntityId layerId_ = {};
    int styleIndex_ = -1;
    StylePaintMode mode_ = StylePaintMode::Color;
    EntityId shaderId_ = {};
    std::optional<PaintSnapshot> oldPaint_;
};

}  // namespace motion
