#include "MotionStudio/model/LayerStylePaint.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/model/GradientType.h"
#include "MotionStudio/model/LayerType.h"
#include "MotionStudio/model/ShaderUniformValues.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/ShapeType.h"
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

bool DefaultGradientEndpoints(const Layer &layer, FrameTime time, Vec2 &outStart, Vec2 &outEnd) {
    outStart = {0.f, 0.f};
    outEnd = {100.f, 0.f};
    if (layer.content == nullptr || layer.content->type() != LayerType::Shape) {
        return false;
    }
    const auto &content = static_cast<const ShapeContent &>(*layer.content);
    if (content.geometry == nullptr) {
        return false;
    }

    Vec2 minPoint{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec2 maxPoint{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    bool hasBounds = false;

    switch (content.geometry->type()) {
        case ShapeType::Rect: {
            const auto &rect = static_cast<const ShapeRect &>(*content.geometry);
            const Vec2 center = rect.position.evaluate(time);
            const Vec2 size = rect.size.evaluate(time);
            const float halfW = std::max(size.x * 0.5f, 0.f);
            const float halfH = std::max(size.y * 0.5f, 0.f);
            minPoint = {center.x - halfW, center.y - halfH};
            maxPoint = {center.x + halfW, center.y + halfH};
            hasBounds = halfW > 0.f || halfH > 0.f;
            break;
        }
        case ShapeType::Ellipse: {
            const auto &ellipse = static_cast<const ShapeEllipse &>(*content.geometry);
            const Vec2 center = ellipse.position.evaluate(time);
            const Vec2 size = ellipse.size.evaluate(time);
            const float halfW = std::max(size.x * 0.5f, 0.f);
            const float halfH = std::max(size.y * 0.5f, 0.f);
            minPoint = {center.x - halfW, center.y - halfH};
            maxPoint = {center.x + halfW, center.y + halfH};
            hasBounds = halfW > 0.f || halfH > 0.f;
            break;
        }
        case ShapeType::Path: {
            const auto &path = static_cast<const ShapePath &>(*content.geometry);
            const VectorNetwork network = path.path.evaluate(time);
            for (const VectorNetwork::Vertex &vertex : network.vertices) {
                minPoint.x = std::min(minPoint.x, vertex.point.x);
                minPoint.y = std::min(minPoint.y, vertex.point.y);
                maxPoint.x = std::max(maxPoint.x, vertex.point.x);
                maxPoint.y = std::max(maxPoint.y, vertex.point.y);
                hasBounds = true;
            }
            break;
        }
        case ShapeType::TrimPath:
            break;
    }

    if (!hasBounds || !(maxPoint.x > minPoint.x) || !(maxPoint.y >= minPoint.y)) {
        return false;
    }
    const float midY = (minPoint.y + maxPoint.y) * 0.5f;
    outStart = {minPoint.x, midY};
    outEnd = {maxPoint.x, midY};
    return true;
}

}  // namespace motion
