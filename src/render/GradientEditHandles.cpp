#include "MotionStudio/render/GradientEditHandles.h"

#include <algorithm>
#include <cmath>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/model/StylePaintMode.h"
#include "MotionStudio/render/ShapeGeometry.h"

namespace motion {
namespace {

constexpr Color kChromeStrokeColor{0.95f, 0.55f, 0.15f, 1.0f};
constexpr Color kChromeFillColor{1.0f, 0.95f, 0.85f, 1.0f};
constexpr float kPi = 3.14159265358979323846f;

float LengthSquared(Vec2 value) {
    return value.x * value.x + value.y * value.y;
}

float Distance(Vec2 a, Vec2 b) {
    return std::sqrt(LengthSquared(a - b));
}

bool IsNearPoint(Vec2 point, Vec2 target, float radius) {
    return LengthSquared(point - target) <= radius * radius;
}

Vec2 AngleOffset(float angleDegrees, float radius) {
    const float radians = angleDegrees * (kPi / 180.0f);
    return {std::cos(radians) * radius, std::sin(radians) * radius};
}

const EvaluatedLayer *FindLayer(const SceneState &state, EntityId id) {
    for (const EvaluatedLayer &layer : state.layers) {
        if (layer.id == id) {
            return &layer;
        }
    }
    return nullptr;
}

const EvaluatedShapeItem *FindGradientItem(const EvaluatedLayer &layer, int styleIndex) {
    for (const EvaluatedShapeItem &item : layer.shapeItems) {
        if (item.styleIndex == styleIndex && item.paint.paintMode == StylePaintMode::Gradient) {
            return &item;
        }
    }
    return nullptr;
}

void AppendStroke(DrawCommandList &commands, ShapeGeometry geometry, float strokeWidth,
                  Color color) {
    DrawCommand command;
    command.type = DrawCommandType::StrokePath;
    command.geometry = std::move(geometry);
    command.paint = Paint{color, FillRule::NonZero};
    command.stroke.width = strokeWidth;
    command.stroke.join = LineJoin::Miter;
    commands.push_back(std::move(command));
}

void AppendFill(DrawCommandList &commands, ShapeGeometry geometry, Color color) {
    DrawCommand command;
    command.type = DrawCommandType::DrawPath;
    command.geometry = std::move(geometry);
    command.paint = Paint{color, FillRule::NonZero};
    commands.push_back(std::move(command));
}

void AppendHandle(DrawCommandList &commands, Vec2 center, float strokeWidth, float handleSize) {
    AppendFill(commands, MakeEllipseGeometry(center, {handleSize, handleSize}), kChromeFillColor);
    AppendStroke(commands, MakeEllipseGeometry(center, {handleSize, handleSize}), strokeWidth,
                 kChromeStrokeColor);
}

ShapeGeometry MakeDiamondOutline(Vec2 center, float radius) {
    BezierPath path = MakeSingleContour(
        {{center + Vec2{0, -radius}, {}, {}},
         {center + Vec2{radius, 0}, {}, {}},
         {center + Vec2{0, radius}, {}, {}},
         {center + Vec2{-radius, 0}, {}, {}}},
        true);
    return MakePathGeometry(std::move(path));
}

}  // namespace

bool BuildGradientEditHandles(const SceneState &state, GradientEditTarget target,
                              GradientEditHandles &out) {
    out = {};
    if (!target.layerId.isValid() || target.styleIndex < 0) {
        return false;
    }
    const EvaluatedLayer *layer = FindLayer(state, target.layerId);
    if (layer == nullptr) {
        return false;
    }
    const EvaluatedShapeItem *item = FindGradientItem(*layer, target.styleIndex);
    if (item == nullptr) {
        return false;
    }
    const EvaluatedGradient &gradient = item->paint.gradient;
    out.valid = true;
    out.target = target;
    out.type = gradient.type;
    out.worldTransform = layer->worldTransform;
    out.worldStart = layer->worldTransform.transformPoint(gradient.start);
    out.worldEnd = layer->worldTransform.transformPoint(gradient.end);
    out.startAngle = gradient.startAngle;
    out.endAngle = gradient.endAngle;
    return true;
}

GradientHandleKind HitTestGradientEdit(const GradientEditHandles &handles, Vec2 scenePoint,
                                       float hitRadius) {
    if (!handles.valid) {
        return GradientHandleKind::None;
    }
    const float radius = std::max(hitRadius, 0.0f);
    if (handles.type == GradientType::Conic) {
        const float rayLength = std::max(Distance(handles.worldStart, handles.worldEnd), 1.0f);
        const Vec2 startAnglePoint = handles.worldStart + AngleOffset(handles.startAngle, rayLength);
        const Vec2 endAnglePoint = handles.worldStart + AngleOffset(handles.endAngle, rayLength);
        if (IsNearPoint(scenePoint, startAnglePoint, radius)) {
            return GradientHandleKind::StartAngle;
        }
        if (IsNearPoint(scenePoint, endAnglePoint, radius)) {
            return GradientHandleKind::EndAngle;
        }
        if (IsNearPoint(scenePoint, handles.worldStart, radius)) {
            return GradientHandleKind::Start;
        }
        return GradientHandleKind::None;
    }
    if (IsNearPoint(scenePoint, handles.worldEnd, radius)) {
        return GradientHandleKind::End;
    }
    if (IsNearPoint(scenePoint, handles.worldStart, radius)) {
        return GradientHandleKind::Start;
    }
    return GradientHandleKind::None;
}

DrawCommandList BuildGradientEditCommands(const GradientEditHandles &handles, float strokeWidth,
                                          float handleSize) {
    DrawCommandList commands;
    if (!handles.valid) {
        return commands;
    }
    const float safeStroke = std::max(strokeWidth, 0.0f);
    const float safeHandle = std::max(handleSize, 1.0f);
    const float rayLength = std::max(Distance(handles.worldStart, handles.worldEnd), 1.0f);

    if (handles.type == GradientType::Linear) {
        BezierPath line =
            MakeSingleContour({{handles.worldStart, {}, {}}, {handles.worldEnd, {}, {}}}, false);
        AppendStroke(commands, MakePathGeometry(std::move(line)), safeStroke, kChromeStrokeColor);
        AppendHandle(commands, handles.worldStart, safeStroke, safeHandle);
        AppendHandle(commands, handles.worldEnd, safeStroke, safeHandle);
        return commands;
    }

    if (handles.type == GradientType::Radial) {
        const float diameter = rayLength * 2.0f;
        AppendStroke(commands, MakeEllipseGeometry(handles.worldStart, {diameter, diameter}),
                     safeStroke, kChromeStrokeColor);
        AppendHandle(commands, handles.worldStart, safeStroke, safeHandle);
        AppendHandle(commands, handles.worldEnd, safeStroke, safeHandle);
        return commands;
    }

    if (handles.type == GradientType::Diamond) {
        AppendStroke(commands, MakeDiamondOutline(handles.worldStart, rayLength), safeStroke,
                     kChromeStrokeColor);
        AppendHandle(commands, handles.worldStart, safeStroke, safeHandle);
        AppendHandle(commands, handles.worldEnd, safeStroke, safeHandle);
        return commands;
    }

    // Conic: center + start/end angle rays.
    const Vec2 startAnglePoint = handles.worldStart + AngleOffset(handles.startAngle, rayLength);
    const Vec2 endAnglePoint = handles.worldStart + AngleOffset(handles.endAngle, rayLength);
    BezierPath startRay =
        MakeSingleContour({{handles.worldStart, {}, {}}, {startAnglePoint, {}, {}}}, false);
    BezierPath endRay =
        MakeSingleContour({{handles.worldStart, {}, {}}, {endAnglePoint, {}, {}}}, false);
    AppendStroke(commands, MakePathGeometry(std::move(startRay)), safeStroke, kChromeStrokeColor);
    AppendStroke(commands, MakePathGeometry(std::move(endRay)), safeStroke, kChromeStrokeColor);
    AppendHandle(commands, handles.worldStart, safeStroke, safeHandle);
    AppendHandle(commands, startAnglePoint, safeStroke, safeHandle);
    AppendHandle(commands, endAnglePoint, safeStroke, safeHandle);
    return commands;
}

}  // namespace motion
