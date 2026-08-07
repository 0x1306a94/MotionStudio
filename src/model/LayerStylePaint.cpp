#include "MotionStudio/model/LayerStylePaint.h"

#include "MotionStudio/model/ShaderUniformValues.h"
#include "MotionStudio/model/StylePaintMode.h"

namespace motion {
namespace {

template <typename Style>
void ClearShaderPaintImpl(Style &style) {
    style.paintMode = StylePaintMode::Color;
    style.shaderId = EntityId{};
    style.uniformValues = ShaderUniformValues{};
}

template <typename Style>
Expected<void, std::string> BindShaderPaintImpl(Style &style, const ShaderDefinition &shader) {
    if (!shader.id.isValid()) {
        return Unexpected(std::string("shader id is invalid"));
    }
    style.paintMode = StylePaintMode::Shader;
    style.shaderId = shader.id;
    style.uniformValues = MakeDefaultUniformValues(shader.uniforms);
    return Expected<void, std::string>();
}

}  // namespace

void ClearShaderPaint(FillStyle &style) {
    ClearShaderPaintImpl(style);
}

void ClearShaderPaint(StrokeStyle &style) {
    ClearShaderPaintImpl(style);
}

Expected<void, std::string> BindShaderPaint(FillStyle &style, const ShaderDefinition &shader) {
    return BindShaderPaintImpl(style, shader);
}

Expected<void, std::string> BindShaderPaint(StrokeStyle &style, const ShaderDefinition &shader) {
    return BindShaderPaintImpl(style, shader);
}

}  // namespace motion
