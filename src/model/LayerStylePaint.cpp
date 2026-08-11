#include "MotionStudio/model/LayerStylePaint.h"

#include <utility>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/model/GradientType.h"
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

bool GradientStopsAreValid(const GradientPaint &gradient) {
    if (gradient.stops.size() < 2u) {
        return false;
    }
    float previous = gradient.stops.front().position.staticValue();
    if (previous != 0.f) {
        return false;
    }
    for (size_t index = 1; index < gradient.stops.size(); ++index) {
        const float position = gradient.stops[index].position.staticValue();
        if (!(position > previous)) {
            return false;
        }
        previous = position;
    }
    return previous == 1.f;
}

void EnsureDefaultGradient(GradientPaint &gradient, Vec2 start, Vec2 end) {
    if (gradient.stops.size() >= 2u) {
        return;
    }
    gradient.type = GradientType::Linear;
    gradient.start.setStaticValue(start);
    gradient.end.setStaticValue(end);
    gradient.startAngle.setStaticValue(0.f);
    gradient.endAngle.setStaticValue(360.f);
    gradient.stops.clear();
    GradientStop startStop;
    startStop.color.setStaticValue(Color{0, 0, 0, 1});
    startStop.position.setStaticValue(0.f);
    GradientStop endStop;
    endStop.color.setStaticValue(Color{1, 1, 1, 1});
    endStop.position.setStaticValue(1.f);
    gradient.stops.push_back(std::move(startStop));
    gradient.stops.push_back(std::move(endStop));
}

}  // namespace motion
