#include "MotionStudio/render/SelectionHandles.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

#include "MotionStudio/render/HitTest.h"
#include "MotionStudio/render/ShapeGeometry.h"

namespace motion {

namespace {

constexpr Color kSelectionChromeColor{0.0f, 0.47843137f, 1.0f, 1.0f};
constexpr Color kHandleFillColor{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color kAnchorFillColor{1.0f, 1.0f, 1.0f, 1.0f};
constexpr float kPi = 3.14159265358979323846f;

float Length(Vec2 value) {
    return std::sqrt(value.x * value.x + value.y * value.y);
}

Vec2 Normalize(Vec2 value) {
    const float length = Length(value);
    if (length <= 1e-6f) {
        return {};
    }
    return value * (1.0f / length);
}

float DistanceSquared(Vec2 a, Vec2 b) {
    const Vec2 delta = a - b;
    return delta.x * delta.x + delta.y * delta.y;
}

float BoxRotationDegrees(Vec2 rightEdge) {
    return std::atan2(rightEdge.y, rightEdge.x) * 180.0f / kPi;
}

void FillEdgeMidsAndCenter(SelectionHandles &handles) {
    for (int index = 0; index < 4; ++index) {
        const Vec2 a = handles.corners[index];
        const Vec2 b = handles.corners[(index + 1) % 4];
        handles.edgeMids[index] = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    }
    handles.center = {(handles.corners[0].x + handles.corners[2].x) * 0.5f,
                      (handles.corners[0].y + handles.corners[2].y) * 0.5f};
}

bool BuildOrientedHandles(const EvaluatedLayer &layer, SelectionHandles &out) {
    Vec2 localMin;
    Vec2 localMax;
    if (!BoundsOfLayerLocal(layer, localMin, localMax)) {
        return false;
    }
    const Vec2 localCorners[4] = {
        {localMin.x, localMin.y},
        {localMax.x, localMin.y},
        {localMax.x, localMax.y},
        {localMin.x, localMax.y},
    };
    for (int index = 0; index < 4; ++index) {
        out.corners[index] = layer.worldTransform.transformPoint(localCorners[index]);
    }
    FillEdgeMidsAndCenter(out);
    out.valid = true;
    out.isOriented = true;
    out.anchor = layer.worldAnchor;
    out.primaryLayerId = layer.id;
    out.localMin = localMin;
    out.localMax = localMax;
    out.boxRotationDegrees = BoxRotationDegrees(out.corners[1] - out.corners[0]);
    return true;
}

bool BuildAxisAlignedUnionHandles(const SceneState &state,
                                  const std::unordered_set<EntityId> &selected,
                                  EntityId primaryLayerId,
                                  SelectionHandles &out) {
    Vec2 minPoint{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec2 maxPoint{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    bool hasBounds = false;
    const EvaluatedLayer *primaryLayer = nullptr;
    for (const EvaluatedLayer &layer : state.layers) {
        if (selected.find(layer.id) == selected.end()) {
            continue;
        }
        if (layer.id == primaryLayerId) {
            primaryLayer = &layer;
        }
        Vec2 layerMin;
        Vec2 layerMax;
        if (!BoundsOfLayer(layer, layerMin, layerMax)) {
            continue;
        }
        minPoint.x = std::min(minPoint.x, layerMin.x);
        minPoint.y = std::min(minPoint.y, layerMin.y);
        maxPoint.x = std::max(maxPoint.x, layerMax.x);
        maxPoint.y = std::max(maxPoint.y, layerMax.y);
        hasBounds = true;
    }
    if (!hasBounds) {
        return false;
    }
    out.corners[0] = {minPoint.x, minPoint.y};
    out.corners[1] = {maxPoint.x, minPoint.y};
    out.corners[2] = {maxPoint.x, maxPoint.y};
    out.corners[3] = {minPoint.x, maxPoint.y};
    FillEdgeMidsAndCenter(out);
    out.valid = true;
    out.isOriented = false;
    out.boxRotationDegrees = 0;
    out.primaryLayerId = primaryLayerId;
    if (primaryLayer != nullptr) {
        out.anchor = primaryLayer->worldAnchor;
    } else {
        out.anchor = out.center;
    }
    return true;
}

ShapeGeometry AxisAlignedSquare(Vec2 center, float size) {
    return MakeRectGeometry(center, {size, size});
}

ShapeGeometry ClosedPolyline(const Vec2 *points, size_t count) {
    BezierPath path;
    path.closed = true;
    path.vertices.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        path.vertices.push_back({points[index], {}, {}});
    }
    return MakePathGeometry(std::move(path));
}

void AppendStroke(DrawCommandList &commands, ShapeGeometry geometry, float strokeWidth) {
    DrawCommand command;
    command.type = DrawCommandType::StrokePath;
    command.geometry = std::move(geometry);
    command.paint = Paint{kSelectionChromeColor, FillRule::NonZero};
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

bool IsNearPoint(Vec2 point, Vec2 target, float radius) {
    return DistanceSquared(point, target) <= radius * radius;
}

}  // namespace

bool BuildSelectionHandles(const SceneState &state,
                           const std::vector<EntityId> &selectedLayerIds,
                           EntityId primaryLayerId,
                           SelectionHandles &out) {
    out = {};
    std::vector<EntityId> ordered;
    ordered.reserve(selectedLayerIds.size());
    std::unordered_set<EntityId> selected;
    selected.reserve(selectedLayerIds.size());
    for (EntityId id : selectedLayerIds) {
        if (!id.isValid() || selected.find(id) != selected.end()) {
            continue;
        }
        selected.insert(id);
        ordered.push_back(id);
    }
    if (ordered.empty()) {
        return false;
    }

    EntityId primary = primaryLayerId;
    if (!primary.isValid() || selected.find(primary) == selected.end()) {
        primary = ordered.back();
    }

    if (ordered.size() == 1) {
        for (const EvaluatedLayer &layer : state.layers) {
            if (layer.id == ordered.front()) {
                return BuildOrientedHandles(layer, out);
            }
        }
        return false;
    }
    return BuildAxisAlignedUnionHandles(state, selected, primary, out);
}

SelectionHandleKind HitTestSelectionHandle(const SelectionHandles &handles,
                                           Vec2 scenePoint,
                                           float handleHitRadius,
                                           float rotateInner,
                                           float rotateOuter) {
    if (!handles.valid) {
        return SelectionHandleKind::None;
    }
    const float safeRadius = std::max(handleHitRadius, 0.0f);
    if (IsNearPoint(scenePoint, handles.anchor, safeRadius)) {
        return SelectionHandleKind::Anchor;
    }

    const SelectionHandleKind cornerKinds[4] = {
        SelectionHandleKind::ScaleCorner0,
        SelectionHandleKind::ScaleCorner1,
        SelectionHandleKind::ScaleCorner2,
        SelectionHandleKind::ScaleCorner3,
    };
    for (int index = 0; index < 4; ++index) {
        if (IsNearPoint(scenePoint, handles.corners[index], safeRadius)) {
            return cornerKinds[index];
        }
    }

    const SelectionHandleKind edgeKinds[4] = {
        SelectionHandleKind::ScaleEdge0,
        SelectionHandleKind::ScaleEdge1,
        SelectionHandleKind::ScaleEdge2,
        SelectionHandleKind::ScaleEdge3,
    };
    for (int index = 0; index < 4; ++index) {
        if (IsNearPoint(scenePoint, handles.edgeMids[index], safeRadius)) {
            return edgeKinds[index];
        }
    }

    const float inner = std::max(rotateInner, 0.0f);
    const float outer = std::max(rotateOuter, inner);
    const SelectionHandleKind rotateKinds[4] = {
        SelectionHandleKind::Rotate0,
        SelectionHandleKind::Rotate1,
        SelectionHandleKind::Rotate2,
        SelectionHandleKind::Rotate3,
    };
    for (int index = 0; index < 4; ++index) {
        const Vec2 corner = handles.corners[index];
        const Vec2 outward = Normalize(corner - handles.center);
        const Vec2 delta = scenePoint - corner;
        const float along = delta.x * outward.x + delta.y * outward.y;
        if (along < inner || along > outer) {
            continue;
        }
        const Vec2 lateral = delta - outward * along;
        if (Length(lateral) <= safeRadius) {
            return rotateKinds[index];
        }
    }
    return SelectionHandleKind::None;
}

DrawCommandList BuildSelectionHandleCommands(const SelectionHandles &handles,
                                             float strokeWidth,
                                             float handleSize,
                                             bool showAnchor) {
    DrawCommandList commands;
    if (!handles.valid) {
        return commands;
    }
    const float safeStroke = std::max(strokeWidth, 0.0f);
    const float safeHandle = std::max(handleSize, safeStroke);
    AppendStroke(commands, ClosedPolyline(handles.corners, 4), safeStroke);

    for (int index = 0; index < 4; ++index) {
        AppendFill(commands, AxisAlignedSquare(handles.corners[index], safeHandle), kHandleFillColor);
        AppendStroke(commands, AxisAlignedSquare(handles.corners[index], safeHandle), safeStroke);
        AppendFill(commands, AxisAlignedSquare(handles.edgeMids[index], safeHandle), kHandleFillColor);
        AppendStroke(commands, AxisAlignedSquare(handles.edgeMids[index], safeHandle), safeStroke);
    }

    if (!showAnchor) {
        return commands;
    }

    const float anchorRadius = safeHandle * 0.55f;
    AppendFill(commands, MakeEllipseGeometry(handles.anchor, {anchorRadius * 2, anchorRadius * 2}),
               kAnchorFillColor);
    AppendStroke(commands, MakeEllipseGeometry(handles.anchor, {anchorRadius * 2, anchorRadius * 2}),
                 safeStroke);
    const float cross = anchorRadius * 1.6f;
    Vec2 horizontal[2] = {{handles.anchor.x - cross, handles.anchor.y},
                          {handles.anchor.x + cross, handles.anchor.y}};
    Vec2 vertical[2] = {{handles.anchor.x, handles.anchor.y - cross},
                        {handles.anchor.x, handles.anchor.y + cross}};
    BezierPath hPath;
    hPath.vertices.push_back({horizontal[0], {}, {}});
    hPath.vertices.push_back({horizontal[1], {}, {}});
    BezierPath vPath;
    vPath.vertices.push_back({vertical[0], {}, {}});
    vPath.vertices.push_back({vertical[1], {}, {}});
    AppendStroke(commands, MakePathGeometry(std::move(hPath)), safeStroke);
    AppendStroke(commands, MakePathGeometry(std::move(vPath)), safeStroke);
    return commands;
}

}  // namespace motion
