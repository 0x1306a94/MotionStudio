#include "MotionStudio/render/PathEditHandles.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "MotionStudio/render/PathOverlay.h"
#include "MotionStudio/render/ShapeGeometry.h"

namespace motion {

namespace {

constexpr Color kPathEditStrokeColor{1.0f, 0.85f, 0.2f, 1.0f};
constexpr Color kHandleFillColor{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color kSelectedHandleFillColor{1.0f, 0.85f, 0.2f, 1.0f};
constexpr Color kTangentFillColor{0.35f, 0.75f, 1.0f, 1.0f};
// Distinct from vertex chrome yellow so tangent stems stay visible.
constexpr Color kTangentStrokeColor{0.25f, 0.55f, 0.95f, 1.0f};
constexpr int kSegmentSamples = 16;

float LengthSquared(Vec2 value) {
    return value.x * value.x + value.y * value.y;
}

bool IsNearPoint(Vec2 point, Vec2 target, float radius) {
    return LengthSquared(point - target) <= radius * radius;
}

Vec2 CubicPoint(Vec2 p0, Vec2 c1, Vec2 c2, Vec2 p3, float t) {
    const float mt = 1.0f - t;
    return p0 * (mt * mt * mt) + c1 * (3.0f * mt * mt * t) + c2 * (3.0f * mt * t * t) +
        p3 * (t * t * t);
}

const EvaluatedLayer *FindLayer(const SceneState &state, EntityId id) {
    for (const EvaluatedLayer &layer : state.layers) {
        if (layer.id == id) {
            return &layer;
        }
    }
    return nullptr;
}

bool ResolveLocalPath(const EvaluatedLayer &layer, PathEditTarget target, BezierPath &outPath) {
    if (target.kind == PathEditKind::Mask) {
        if (target.maskIndex < 0 ||
            static_cast<size_t>(target.maskIndex) >= layer.masks.size()) {
            return false;
        }
        outPath = layer.masks[static_cast<size_t>(target.maskIndex)].path;
        return !outPath.vertices.empty();
    }
    if (layer.shapeItems.empty()) {
        return false;
    }
    outPath = ShapeGeometryToBezierPath(layer.shapeItems.front().geometry);
    return !outPath.vertices.empty();
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

ShapeGeometry AxisAlignedSquare(Vec2 center, float size) {
    return MakeRectGeometry(center, {size, size});
}

bool ClosestOnSegment(const BezierPath &path, size_t segmentIndex, Vec2 localPoint,
                      float &outT, float &outDistanceSquared) {
    const size_t count = path.vertices.size();
    const BezierPath::Vertex &from = path.vertices[segmentIndex];
    const BezierPath::Vertex &to = path.vertices[(segmentIndex + 1) % count];
    const Vec2 c1 = from.point + from.outTangent;
    const Vec2 c2 = to.point + to.inTangent;

    float bestDistance = LengthSquared(localPoint - from.point);
    float bestT = 0.0f;
    for (int sample = 1; sample <= kSegmentSamples; ++sample) {
        const float t = static_cast<float>(sample) / static_cast<float>(kSegmentSamples);
        const Vec2 point = CubicPoint(from.point, c1, c2, to.point, t);
        const float distance = LengthSquared(localPoint - point);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestT = t;
        }
    }
    outT = bestT;
    outDistanceSquared = bestDistance;
    return true;
}

}  // namespace

bool BuildPathEditHandlesFromPath(const BezierPath &localPath, Mat3 worldTransform,
                                  PathEditTarget target, int selectedVertex,
                                  PathEditHandles &out) {
    out = {};
    if (localPath.vertices.empty()) {
        return false;
    }
    out.valid = true;
    out.target = target;
    out.localPath = localPath;
    out.worldTransform = worldTransform;
    out.selectedVertex = selectedVertex;
    out.worldVertices.reserve(localPath.vertices.size());
    out.worldInHandles.reserve(localPath.vertices.size());
    out.worldOutHandles.reserve(localPath.vertices.size());
    for (const BezierPath::Vertex &vertex : localPath.vertices) {
        out.worldVertices.push_back(worldTransform.transformPoint(vertex.point));
        out.worldInHandles.push_back(
            worldTransform.transformPoint(vertex.point + vertex.inTangent));
        out.worldOutHandles.push_back(
            worldTransform.transformPoint(vertex.point + vertex.outTangent));
    }
    if (selectedVertex < 0 ||
        static_cast<size_t>(selectedVertex) >= localPath.vertices.size()) {
        out.selectedVertex = -1;
    }
    return true;
}

bool BuildPathEditHandles(const SceneState &state, PathEditTarget target, int selectedVertex,
                          PathEditHandles &out) {
    out = {};
    if (!target.layerId.isValid()) {
        return false;
    }
    const EvaluatedLayer *layer = FindLayer(state, target.layerId);
    if (layer == nullptr) {
        return false;
    }
    BezierPath localPath;
    if (!ResolveLocalPath(*layer, target, localPath)) {
        return false;
    }
    return BuildPathEditHandlesFromPath(localPath, layer->worldTransform, target, selectedVertex,
                                        out);
}

PathEditHit HitTestPathEdit(const PathEditHandles &handles, Vec2 scenePoint,
                            float handleHitRadius, float segmentHitRadius) {
    PathEditHit hit;
    if (!handles.valid) {
        return hit;
    }
    const float handleRadius = std::max(handleHitRadius, 0.0f);
    const float segmentRadius = std::max(segmentHitRadius, 0.0f);

    if (handles.selectedVertex >= 0) {
        const size_t selected = static_cast<size_t>(handles.selectedVertex);
        const Vec2 vertex = handles.worldVertices[selected];
        if (!IsNearPoint(handles.worldInHandles[selected], vertex, 1e-4f) &&
            IsNearPoint(scenePoint, handles.worldInHandles[selected], handleRadius)) {
            hit.kind = PathHandleKind::InTangent;
            hit.index = selected;
            return hit;
        }
        if (!IsNearPoint(handles.worldOutHandles[selected], vertex, 1e-4f) &&
            IsNearPoint(scenePoint, handles.worldOutHandles[selected], handleRadius)) {
            hit.kind = PathHandleKind::OutTangent;
            hit.index = selected;
            return hit;
        }
    }

    for (size_t index = 0; index < handles.worldVertices.size(); ++index) {
        if (!IsNearPoint(scenePoint, handles.worldVertices[index], handleRadius)) {
            continue;
        }
        if (!handles.localPath.closed && index == 0 && handles.worldVertices.size() >= 2) {
            hit.kind = PathHandleKind::CloseRing;
            hit.index = 0;
            return hit;
        }
        hit.kind = PathHandleKind::Vertex;
        hit.index = index;
        return hit;
    }

    Mat3 inverse;
    if (!handles.worldTransform.tryInvert(inverse)) {
        return hit;
    }
    const Vec2 localPoint = inverse.transformPoint(scenePoint);
    const size_t count = handles.localPath.vertices.size();
    const size_t segmentCount = handles.localPath.closed ? count : (count > 0 ? count - 1 : 0);
    const float radiusSquared = segmentRadius * segmentRadius;
    float bestDistance = radiusSquared;
    bool found = false;
    for (size_t segment = 0; segment < segmentCount; ++segment) {
        float t = 0;
        float distance = 0;
        ClosestOnSegment(handles.localPath, segment, localPoint, t, distance);
        if (distance <= bestDistance) {
            bestDistance = distance;
            hit.kind = PathHandleKind::Segment;
            hit.index = segment;
            hit.segmentT = t;
            found = true;
        }
    }
    if (!found) {
        hit = {};
    }
    return hit;
}

DrawCommandList BuildPathEditCommands(const PathEditHandles &handles, float strokeWidth,
                                      float handleSize) {
    DrawCommandList commands;
    if (!handles.valid) {
        return commands;
    }
    const float safeStroke = std::max(strokeWidth, 0.0f);
    const float safeHandle = std::max(handleSize, safeStroke);

    PathOverlayItem overlay;
    overlay.worldTransform = handles.worldTransform;
    overlay.path = handles.localPath;
    overlay.color = kPathEditStrokeColor;
    DrawCommandList strokeCommands = BuildPathOverlayCommands({overlay}, safeStroke);
    commands.insert(commands.end(), strokeCommands.begin(), strokeCommands.end());

    if (handles.selectedVertex >= 0) {
        const size_t selected = static_cast<size_t>(handles.selectedVertex);
        const Vec2 vertex = handles.worldVertices[selected];
        const Vec2 inHandle = handles.worldInHandles[selected];
        const Vec2 outHandle = handles.worldOutHandles[selected];

        if (!IsNearPoint(inHandle, vertex, 1e-4f)) {
            BezierPath inLine;
            inLine.vertices.push_back({vertex, {}, {}});
            inLine.vertices.push_back({inHandle, {}, {}});
            AppendStroke(commands, MakePathGeometry(std::move(inLine)), safeStroke,
                         kTangentStrokeColor);
            const float tangentSize = safeHandle * 0.85f;
            AppendFill(commands, MakeEllipseGeometry(inHandle, {tangentSize, tangentSize}),
                       kTangentFillColor);
            AppendStroke(commands, MakeEllipseGeometry(inHandle, {tangentSize, tangentSize}),
                         safeStroke, kTangentStrokeColor);
        }
        if (!IsNearPoint(outHandle, vertex, 1e-4f)) {
            BezierPath outLine;
            outLine.vertices.push_back({vertex, {}, {}});
            outLine.vertices.push_back({outHandle, {}, {}});
            AppendStroke(commands, MakePathGeometry(std::move(outLine)), safeStroke,
                         kTangentStrokeColor);
            const float tangentSize = safeHandle * 0.85f;
            AppendFill(commands, MakeEllipseGeometry(outHandle, {tangentSize, tangentSize}),
                       kTangentFillColor);
            AppendStroke(commands, MakeEllipseGeometry(outHandle, {tangentSize, tangentSize}),
                         safeStroke, kTangentStrokeColor);
        }
    }

    for (size_t index = 0; index < handles.worldVertices.size(); ++index) {
        const Vec2 &vertex = handles.worldVertices[index];
        const bool selected = handles.selectedVertex >= 0 &&
            static_cast<size_t>(handles.selectedVertex) == index;
        AppendFill(commands, AxisAlignedSquare(vertex, safeHandle),
                   selected ? kSelectedHandleFillColor : kHandleFillColor);
        AppendStroke(commands, AxisAlignedSquare(vertex, safeHandle), safeStroke,
                     kPathEditStrokeColor);
    }
    return commands;
}

}  // namespace motion
