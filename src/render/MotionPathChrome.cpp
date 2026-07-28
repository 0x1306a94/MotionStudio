#include "MotionStudio/render/MotionPathChrome.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/animation/MotionPath.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/render/PathOverlay.h"
#include "MotionStudio/render/ShapeGeometry.h"

namespace motion {

namespace {

constexpr Color kMotionPathStrokeColor{0.35f, 0.75f, 1.0f, 1.0f};
constexpr Color kHandleFillColor{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color kSelectedHandleFillColor{0.35f, 0.75f, 1.0f, 1.0f};
constexpr Color kTangentFillColor{0.35f, 0.75f, 1.0f, 1.0f};
constexpr Color kTangentStrokeColor{0.25f, 0.55f, 0.95f, 1.0f};

float LengthSquared(Vec2 value) {
    return value.x * value.x + value.y * value.y;
}

bool IsNearPoint(Vec2 point, Vec2 target, float radius) {
    return LengthSquared(point - target) <= radius * radius;
}

Mat3 LocalMatrixOf(const Transform &transform, PreviewTime time) {
    return Mat3::Translate(transform.position.evaluatePreview(time)) *
        Mat3::Rotate(transform.rotation.evaluatePreview(time)) *
        Mat3::Scale(transform.scale.evaluatePreview(time)) *
        Mat3::Translate(-transform.anchorPoint.evaluatePreview(time));
}

Mat3 WorldMatrixOf(const Document &document, const Layer &layer, PreviewTime time, int depth) {
    const Mat3 local = LocalMatrixOf(layer.transform, time);
    if (!layer.parentId.isValid() || depth >= 1024) {
        return local;
    }
    const Layer *parent = document.entityIndex().findLayer(layer.parentId);
    if (parent == nullptr) {
        return local;
    }
    return WorldMatrixOf(document, *parent, time, depth + 1) * local;
}

Mat3 ParentWorldMatrixOf(const Document &document, const Layer &layer, PreviewTime time) {
    if (!layer.parentId.isValid()) {
        return Mat3::Identity();
    }
    const Layer *parent = document.entityIndex().findLayer(layer.parentId);
    if (parent == nullptr) {
        return Mat3::Identity();
    }
    return WorldMatrixOf(document, *parent, time, 0);
}

Vec2 DefaultOutTangent(const std::vector<Keyframe<Vec2>> &keyframes, size_t index) {
    if (index + 1 >= keyframes.size()) {
        return {};
    }
    return (keyframes[index + 1].value - keyframes[index].value) * (1.0f / 3.0f);
}

Vec2 DefaultInTangent(const std::vector<Keyframe<Vec2>> &keyframes, size_t index) {
    if (index == 0) {
        return {};
    }
    return (keyframes[index - 1].value - keyframes[index].value) * (1.0f / 3.0f);
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

}  // namespace

bool BuildMotionPathChrome(const Document &document, EntityId layerId, PreviewTime time,
                           int selectedKeyframe, MotionPathChrome &out) {
    out = {};
    if (!layerId.isValid()) {
        return false;
    }
    const Layer *layer = document.entityIndex().findLayer(layerId);
    if (layer == nullptr) {
        return false;
    }
    const Animatable<Vec2> &position = layer->transform.position;
    const std::vector<Keyframe<Vec2>> &keyframes = position.keyframes();
    if (keyframes.size() < 2) {
        return false;
    }

    out.valid = true;
    out.layerId = layerId;
    out.path = BuildMotionPath(position);
    out.parentWorldTransform = ParentWorldMatrixOf(document, *layer, time);
    out.selectedKeyframe = selectedKeyframe;
    if (selectedKeyframe < 0 ||
        static_cast<size_t>(selectedKeyframe) >= keyframes.size()) {
        out.selectedKeyframe = -1;
    }

    out.keyframeTimes.reserve(keyframes.size());
    out.localVertices.reserve(keyframes.size());
    out.displayInTangents.reserve(keyframes.size());
    out.displayOutTangents.reserve(keyframes.size());
    out.hasStoredIn.reserve(keyframes.size());
    out.hasStoredOut.reserve(keyframes.size());
    out.worldVertices.reserve(keyframes.size());
    out.worldInHandles.reserve(keyframes.size());
    out.worldOutHandles.reserve(keyframes.size());

    for (size_t index = 0; index < keyframes.size(); ++index) {
        const Keyframe<Vec2> &keyframe = keyframes[index];
        out.keyframeTimes.push_back(keyframe.time);
        out.localVertices.push_back(keyframe.value);

        const bool hasIn = keyframe.spatialInTangent.has_value();
        const bool hasOut = keyframe.spatialOutTangent.has_value();
        out.hasStoredIn.push_back(hasIn);
        out.hasStoredOut.push_back(hasOut);

        const Vec2 inTangent = hasIn ? *keyframe.spatialInTangent : DefaultInTangent(keyframes, index);
        const Vec2 outTangent =
            hasOut ? *keyframe.spatialOutTangent : DefaultOutTangent(keyframes, index);
        out.displayInTangents.push_back(inTangent);
        out.displayOutTangents.push_back(outTangent);

        out.worldVertices.push_back(out.parentWorldTransform.transformPoint(keyframe.value));
        out.worldInHandles.push_back(
            out.parentWorldTransform.transformPoint(keyframe.value + inTangent));
        out.worldOutHandles.push_back(
            out.parentWorldTransform.transformPoint(keyframe.value + outTangent));
    }
    return true;
}

MotionPathHit HitTestMotionPath(const MotionPathChrome &chrome, Vec2 scenePoint,
                                float handleHitRadius) {
    MotionPathHit hit;
    if (!chrome.valid) {
        return hit;
    }
    const float radius = std::max(handleHitRadius, 0.0f);

    if (chrome.selectedKeyframe >= 0) {
        const size_t selected = static_cast<size_t>(chrome.selectedKeyframe);
        const Vec2 vertex = chrome.worldVertices[selected];
        if (selected < chrome.worldInHandles.size() &&
            (selected > 0 || chrome.hasStoredIn[selected]) &&
            !IsNearPoint(chrome.worldInHandles[selected], vertex, 1e-4f) &&
            IsNearPoint(scenePoint, chrome.worldInHandles[selected], radius)) {
            hit.kind = MotionPathHandleKind::InTangent;
            hit.index = selected;
            return hit;
        }
        if (selected < chrome.worldOutHandles.size() &&
            (selected + 1 < chrome.worldVertices.size() || chrome.hasStoredOut[selected]) &&
            !IsNearPoint(chrome.worldOutHandles[selected], vertex, 1e-4f) &&
            IsNearPoint(scenePoint, chrome.worldOutHandles[selected], radius)) {
            hit.kind = MotionPathHandleKind::OutTangent;
            hit.index = selected;
            return hit;
        }
    }

    float bestDistanceSquared = radius * radius;
    int bestKeyframe = -1;
    for (size_t index = 0; index < chrome.worldVertices.size(); ++index) {
        const float distanceSquared = LengthSquared(scenePoint - chrome.worldVertices[index]);
        if (distanceSquared <= bestDistanceSquared) {
            bestDistanceSquared = distanceSquared;
            bestKeyframe = static_cast<int>(index);
        }
    }
    if (bestKeyframe >= 0) {
        hit.kind = MotionPathHandleKind::Keyframe;
        hit.index = static_cast<size_t>(bestKeyframe);
    }
    return hit;
}

DrawCommandList BuildMotionPathCommands(const MotionPathChrome &chrome, float strokeWidth,
                                        float handleSize) {
    DrawCommandList commands;
    if (!chrome.valid) {
        return commands;
    }
    const float safeStroke = std::max(strokeWidth, 0.0f);
    const float safeHandle = std::max(handleSize, safeStroke);

    PathOverlayItem overlay;
    overlay.worldTransform = chrome.parentWorldTransform;
    overlay.path = chrome.path;
    overlay.color = kMotionPathStrokeColor;
    DrawCommandList strokeCommands = BuildPathOverlayCommands({overlay}, safeStroke);
    commands.insert(commands.end(), strokeCommands.begin(), strokeCommands.end());

    if (chrome.selectedKeyframe >= 0) {
        const size_t selected = static_cast<size_t>(chrome.selectedKeyframe);
        const Vec2 vertex = chrome.worldVertices[selected];
        const Vec2 inHandle = chrome.worldInHandles[selected];
        const Vec2 outHandle = chrome.worldOutHandles[selected];
        const float tangentSize = safeHandle * 0.85f;

        if (selected > 0 || chrome.hasStoredIn[selected]) {
            BezierPath inLine;
            inLine.vertices.push_back({vertex, {}, {}});
            inLine.vertices.push_back({inHandle, {}, {}});
            AppendStroke(commands, MakePathGeometry(std::move(inLine)), safeStroke,
                         kTangentStrokeColor);
            AppendFill(commands, MakeEllipseGeometry(inHandle, {tangentSize, tangentSize}),
                       kTangentFillColor);
            AppendStroke(commands, MakeEllipseGeometry(inHandle, {tangentSize, tangentSize}),
                         safeStroke, kTangentStrokeColor);
        }
        if (selected + 1 < chrome.worldVertices.size() || chrome.hasStoredOut[selected]) {
            BezierPath outLine;
            outLine.vertices.push_back({vertex, {}, {}});
            outLine.vertices.push_back({outHandle, {}, {}});
            AppendStroke(commands, MakePathGeometry(std::move(outLine)), safeStroke,
                         kTangentStrokeColor);
            AppendFill(commands, MakeEllipseGeometry(outHandle, {tangentSize, tangentSize}),
                       kTangentFillColor);
            AppendStroke(commands, MakeEllipseGeometry(outHandle, {tangentSize, tangentSize}),
                         safeStroke, kTangentStrokeColor);
        }
    }

    for (size_t index = 0; index < chrome.worldVertices.size(); ++index) {
        const Vec2 &vertex = chrome.worldVertices[index];
        const bool selected = chrome.selectedKeyframe >= 0 &&
            static_cast<size_t>(chrome.selectedKeyframe) == index;
        AppendFill(commands, AxisAlignedSquare(vertex, safeHandle),
                   selected ? kSelectedHandleFillColor : kHandleFillColor);
        AppendStroke(commands, AxisAlignedSquare(vertex, safeHandle), safeStroke,
                     kMotionPathStrokeColor);
    }
    return commands;
}

std::vector<MotionPathSpatialUpdate> MotionPathTangentDragUpdates(
    const Document &document, EntityId layerId, size_t keyframeIndex, bool isOut,
    Vec2 scenePoint, Mat3 parentWorldTransform) {
    std::vector<MotionPathSpatialUpdate> updates;
    const Layer *layer = document.entityIndex().findLayer(layerId);
    if (layer == nullptr) {
        return updates;
    }
    const Animatable<Vec2> &position = layer->transform.position;
    const std::vector<Keyframe<Vec2>> &keyframes = position.keyframes();
    if (keyframeIndex >= keyframes.size()) {
        return updates;
    }

    Mat3 inverse;
    if (!parentWorldTransform.tryInvert(inverse)) {
        return updates;
    }
    const Vec2 localPoint = inverse.transformPoint(scenePoint);
    const Keyframe<Vec2> &keyframe = keyframes[keyframeIndex];
    const Vec2 tangent = localPoint - keyframe.value;

    MotionPathSpatialUpdate self;
    self.time = keyframe.time;
    self.spatialIn = keyframe.spatialInTangent;
    self.spatialOut = keyframe.spatialOutTangent;
    if (isOut) {
        self.spatialOut = tangent;
    } else {
        self.spatialIn = tangent;
    }
    updates.push_back(self);

    if (isOut && keyframeIndex + 1 < keyframes.size() &&
        !keyframes[keyframeIndex + 1].spatialInTangent.has_value()) {
        MotionPathSpatialUpdate neighbor;
        neighbor.time = keyframes[keyframeIndex + 1].time;
        neighbor.spatialIn = DefaultInTangent(keyframes, keyframeIndex + 1);
        neighbor.spatialOut = keyframes[keyframeIndex + 1].spatialOutTangent;
        updates.push_back(neighbor);
    }
    if (!isOut && keyframeIndex > 0 &&
        !keyframes[keyframeIndex - 1].spatialOutTangent.has_value()) {
        MotionPathSpatialUpdate neighbor;
        neighbor.time = keyframes[keyframeIndex - 1].time;
        neighbor.spatialIn = keyframes[keyframeIndex - 1].spatialInTangent;
        neighbor.spatialOut = DefaultOutTangent(keyframes, keyframeIndex - 1);
        updates.push_back(neighbor);
    }
    return updates;
}

}  // namespace motion
