#include "MotionStudio/render/FollowPathEval.h"

#include <algorithm>
#include <cmath>

#include "MotionStudio/animation/PathSampling.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerType.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/ShapeType.h"
#include "MotionStudio/render/ShapeGeometry.h"

namespace motion {
namespace {

bool ContainsId(const std::vector<EntityId> &ids, EntityId id) {
    for (const EntityId &visited : ids) {
        if (visited == id) {
            return true;
        }
    }
    return false;
}

Mat3 LocalMatrixWithFollow(const Document &document, const Layer &layer, PreviewTime time,
                           const Mat3 &context, const Mat3 &parentWorld,
                           std::vector<EntityId> &followVisiting) {
    const Transform &transform = layer.transform;
    Vec2 position = transform.position.evaluatePreview(time);
    float rotation = transform.rotation.evaluatePreview(time);
    const std::optional<FollowSample> sample =
        EvaluateFollowPath(document, layer, time, context, parentWorld, followVisiting);
    if (sample) {
        position = sample->parentSpacePosition;
        if (sample->overrideRotation) {
            rotation = sample->rotationDegrees;
        }
    }
    return Mat3::Translate(position) * Mat3::Rotate(rotation) *
        Mat3::Scale(transform.scale.evaluatePreview(time)) *
        Mat3::Translate(-transform.anchorPoint.evaluatePreview(time));
}

}  // namespace

std::optional<BezierPath> EvaluateLayerPath(const Layer &layer, PreviewTime time) {
    if (layer.content == nullptr || layer.content->type() != LayerType::Shape) {
        return std::nullopt;
    }
    const auto &shapeContent = static_cast<const ShapeContent &>(*layer.content);
    if (shapeContent.geometry == nullptr) {
        return std::nullopt;
    }
    const ShapeElement &element = *shapeContent.geometry;
    switch (element.type()) {
        case ShapeType::Path: {
            const auto &shape = static_cast<const ShapePath &>(element);
            BezierPath path = shape.path.evaluatePreview(time);
            if (path.vertices.empty()) {
                return std::nullopt;
            }
            return path;
        }
        case ShapeType::Rect: {
            const auto &shape = static_cast<const ShapeRect &>(element);
            const Vec2 center = shape.position.evaluatePreview(time);
            const Vec2 size = shape.size.evaluatePreview(time);
            const float halfWidth = std::max(size.x * 0.5f, 0.0f);
            const float halfHeight = std::max(size.y * 0.5f, 0.0f);
            const float cornerRadius = std::clamp(shape.cornerRadius.evaluatePreview(time), 0.0f,
                                                  std::min(halfWidth, halfHeight));
            return ShapeGeometryToBezierPath(MakeRectGeometry(center, size, cornerRadius));
        }
        case ShapeType::Ellipse: {
            const auto &shape = static_cast<const ShapeEllipse &>(element);
            return ShapeGeometryToBezierPath(
                MakeEllipseGeometry(shape.position.evaluatePreview(time),
                                    shape.size.evaluatePreview(time)));
        }
        case ShapeType::TrimPath: {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<FollowSample> EvaluateFollowPath(const Document &document, const Layer &layer,
                                               PreviewTime time, const Mat3 &context,
                                               const Mat3 &parentWorld,
                                               std::vector<EntityId> &followVisiting) {
    const FollowPath &follow = layer.followPath;
    if (!follow.enabled || !follow.pathLayerId.isValid() || follow.pathLayerId == layer.id) {
        return std::nullopt;
    }
    if (ContainsId(followVisiting, layer.id)) {
        return std::nullopt;
    }
    const Layer *pathLayer = document.entityIndex().findLayer(follow.pathLayerId);
    if (pathLayer == nullptr) {
        return std::nullopt;
    }
    const std::optional<BezierPath> path = EvaluateLayerPath(*pathLayer, time);
    if (!path) {
        return std::nullopt;
    }

    followVisiting.push_back(layer.id);
    std::vector<EntityId> pathParentVisiting;
    const Mat3 pathWorld = FollowAwareWorldTransform(document, *pathLayer, time, context,
                                                     pathParentVisiting, followVisiting);
    followVisiting.pop_back();

    const float totalLength = PathArcLength(*path);
    const float offset =
        std::clamp(follow.pathOffset.evaluatePreview(time), 0.0f, 1.0f);
    const PathSample pathSample =
        PointAndTangentAtArcLength(*path, offset * totalLength);

    const Vec2 worldPoint = pathWorld.transformPoint(pathSample.point);
    Mat3 parentInverse = Mat3::Identity();
    if (!parentWorld.tryInvert(parentInverse)) {
        return std::nullopt;
    }

    FollowSample sample;
    sample.parentSpacePosition = parentInverse.transformPoint(worldPoint);
    sample.overrideRotation = follow.orientAlongPath;
    if (follow.orientAlongPath) {
        const Vec2 worldTangent = pathWorld.transformVector(pathSample.tangent);
        const float radians = std::atan2(worldTangent.y, worldTangent.x);
        const float orientOffset = follow.orientOffset.evaluatePreview(time);
        sample.rotationDegrees = radians * (180.0f / static_cast<float>(M_PI)) + orientOffset;
    }
    return sample;
}

Mat3 FollowAwareWorldTransform(const Document &document, const Layer &layer, PreviewTime time,
                               const Mat3 &context, std::vector<EntityId> &parentVisiting,
                               std::vector<EntityId> &followVisiting) {
    if (ContainsId(parentVisiting, layer.id)) {
        return context;
    }
    parentVisiting.push_back(layer.id);
    Mat3 parentWorld = context;
    if (layer.parentId.isValid()) {
        const Layer *parent = document.entityIndex().findLayer(layer.parentId);
        if (parent != nullptr) {
            parentWorld = FollowAwareWorldTransform(document, *parent, time, context,
                                                    parentVisiting, followVisiting);
        }
    }
    const Mat3 local =
        LocalMatrixWithFollow(document, layer, time, context, parentWorld, followVisiting);
    parentVisiting.pop_back();
    return parentWorld * local;
}

}  // namespace motion
