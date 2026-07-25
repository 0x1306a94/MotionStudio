#include "MotionStudio/render/SceneEvaluator.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/PrecompContent.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapeGroup.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"

namespace motion {

namespace {

constexpr int kMaxPrecompDepth = 1024;
constexpr float kEllipseKappa = 0.5522847498f;

Mat3 LocalMatrixOf(const Transform &transform, PreviewTime time) {
    return Mat3::Translate(transform.position.evaluatePreview(time)) *
        Mat3::Rotate(transform.rotation.evaluatePreview(time)) *
        Mat3::Scale(transform.scale.evaluatePreview(time)) *
        Mat3::Translate(-transform.anchorPoint.evaluatePreview(time));
}

// Applies the matrix to a path; tangents take the linear part only.
BezierPath TransformPath(const BezierPath &path, const Mat3 &matrix) {
    BezierPath result;
    result.closed = path.closed;
    result.vertices.reserve(path.vertices.size());
    for (const BezierPath::Vertex &vertex : path.vertices) {
        result.vertices.push_back({matrix.transformPoint(vertex.point),
                                   matrix.transformVector(vertex.inTangent),
                                   matrix.transformVector(vertex.outTangent)});
    }
    return result;
}

// Rect centered at position (Lottie convention), corners clockwise from
// top-left; rounded corners split each corner into an arc pair.
BezierPath RectToPath(const ShapeRect &rect, PreviewTime time) {
    const Vec2 center = rect.position.evaluatePreview(time);
    const Vec2 size = rect.size.evaluatePreview(time);
    const float halfWidth = std::max(size.x * 0.5f, 0.0f);
    const float halfHeight = std::max(size.y * 0.5f, 0.0f);
    const float radius = std::clamp(rect.cornerRadius.evaluatePreview(time), 0.0f,
                                    std::min(halfWidth, halfHeight));
    const float left = center.x - halfWidth;
    const float right = center.x + halfWidth;
    const float top = center.y - halfHeight;
    const float bottom = center.y + halfHeight;

    BezierPath path;
    path.closed = true;
    if (radius <= 0) {
        path.vertices.push_back({{left, top}, {}, {}});
        path.vertices.push_back({{right, top}, {}, {}});
        path.vertices.push_back({{right, bottom}, {}, {}});
        path.vertices.push_back({{left, bottom}, {}, {}});
        return path;
    }
    const float k = kEllipseKappa * radius;
    path.vertices.push_back({{left, top + radius}, {}, {0, -k}});
    path.vertices.push_back({{left + radius, top}, {-k, 0}, {}});
    path.vertices.push_back({{right - radius, top}, {}, {k, 0}});
    path.vertices.push_back({{right, top + radius}, {0, -k}, {}});
    path.vertices.push_back({{right, bottom - radius}, {}, {0, k}});
    path.vertices.push_back({{right - radius, bottom}, {k, 0}, {}});
    path.vertices.push_back({{left + radius, bottom}, {}, {-k, 0}});
    path.vertices.push_back({{left, bottom - radius}, {0, k}, {}});
    return path;
}

// Ellipse centered at position, approximated by 4 cubic segments.
BezierPath EllipseToPath(const ShapeEllipse &ellipse, PreviewTime time) {
    const Vec2 center = ellipse.position.evaluatePreview(time);
    const Vec2 size = ellipse.size.evaluatePreview(time);
    const float halfWidth = std::max(size.x * 0.5f, 0.0f);
    const float halfHeight = std::max(size.y * 0.5f, 0.0f);
    const float kx = kEllipseKappa * halfWidth;
    const float ky = kEllipseKappa * halfHeight;

    BezierPath path;
    path.closed = true;
    path.vertices.push_back({{center.x + halfWidth, center.y}, {0, -ky}, {0, ky}});
    path.vertices.push_back({{center.x, center.y + halfHeight}, {kx, 0}, {-kx, 0}});
    path.vertices.push_back({{center.x - halfWidth, center.y}, {0, ky}, {0, -ky}});
    path.vertices.push_back({{center.x, center.y - halfHeight}, {-kx, 0}, {kx, 0}});
    return path;
}

void CollectGeometryPath(const ShapeElement &element, PreviewTime time,
                         const Mat3 &transform, std::vector<BezierPath> &paths);

void CollectGeometryPaths(const std::vector<std::unique_ptr<ShapeElement>> &elements,
                          PreviewTime time, const Mat3 &transform,
                          std::vector<BezierPath> &paths) {
    for (const auto &element : elements) {
        CollectGeometryPath(*element, time, transform, paths);
    }
}

void CollectGeometryPath(const ShapeElement &element, PreviewTime time,
                         const Mat3 &transform, std::vector<BezierPath> &paths) {
    switch (element.type()) {
        case ShapeType::Path: {
            const auto &shape = static_cast<const ShapePath &>(element);
            paths.push_back(TransformPath(shape.path.evaluatePreview(time), transform));
            break;
        }
        case ShapeType::Rect: {
            const auto &shape = static_cast<const ShapeRect &>(element);
            paths.push_back(TransformPath(RectToPath(shape, time), transform));
            break;
        }
        case ShapeType::Ellipse: {
            const auto &shape = static_cast<const ShapeEllipse &>(element);
            paths.push_back(TransformPath(EllipseToPath(shape, time), transform));
            break;
        }
        case ShapeType::Group: {
            const auto &group = static_cast<const ShapeGroup &>(element);
            CollectGeometryPaths(group.elements, time,
                                 transform * LocalMatrixOf(group.transform, time),
                                 paths);
            break;
        }
        case ShapeType::TrimPath: {
            break;
        }
    }
}

void ApplyLayerStyles(const Layer &layer, PreviewTime time, float alpha,
                      const std::vector<BezierPath> &paths,
                      std::vector<EvaluatedShapeItem> &items) {
    for (const auto &style : layer.styles) {
        switch (style->type()) {
            case LayerStyleType::Fill: {
                const auto &fill = static_cast<const FillStyle &>(*style);
                Color color = fill.color.evaluatePreview(time);
                color.a *= alpha;
                const Paint paint{color, fill.fillRule, fill.blendMode};
                for (const BezierPath &path : paths) {
                    items.push_back({path, paint, false, 0, LineCap::Butt, LineJoin::Miter});
                }
                break;
            }
            case LayerStyleType::Stroke: {
                const auto &stroke = static_cast<const StrokeStyle &>(*style);
                Color color = stroke.color.evaluatePreview(time);
                color.a *= alpha;
                // Strokes have no blend mode of their own; they keep the layer's.
                const Paint paint{color, FillRule::NonZero, layer.blendMode};
                const float width = stroke.width.evaluatePreview(time);
                for (const BezierPath &path : paths) {
                    items.push_back({path, paint, true, width, stroke.cap, stroke.join});
                }
                break;
            }
        }
    }
}

// World transform with parent-chain walking; context is the transform handed
// down by the enclosing precomp. visiting guards against parent cycles.
Mat3 WorldTransformOf(const Document &document, const Layer &layer, PreviewTime time,
                      const Mat3 &context, std::vector<EntityId> &visiting) {
    for (const EntityId &visited : visiting) {
        if (visited == layer.id) {
            return context;
        }
    }
    visiting.push_back(layer.id);
    const Mat3 local = LocalMatrixOf(layer.transform, time);
    Mat3 result = context * local;
    if (layer.parentId.isValid()) {
        const Layer *parent = document.entityIndex().findLayer(layer.parentId);
        if (parent) {
            result = WorldTransformOf(document, *parent, time, context, visiting) * local;
        }
    }
    visiting.pop_back();
    return result;
}

float WorldOpacityOf(const Document &document, const Layer &layer, PreviewTime time,
                     float context, std::vector<EntityId> &visiting) {
    for (const EntityId &visited : visiting) {
        if (visited == layer.id) {
            return context;
        }
    }
    visiting.push_back(layer.id);
    const float own = layer.transform.opacity.evaluatePreview(time);
    float result = context * own;
    if (layer.parentId.isValid()) {
        const Layer *parent = document.entityIndex().findLayer(layer.parentId);
        if (parent) {
            result = WorldOpacityOf(document, *parent, time, context, visiting) * own;
        }
    }
    visiting.pop_back();
    return result;
}

void EvaluateComposition(const Document &document, const Composition &composition,
                         PreviewTime time, const Mat3 &contextTransform,
                         float contextOpacity, int depth,
                         std::vector<EvaluatedLayer> &out);

void EvaluateLayer(const Document &document, const Layer &layer, PreviewTime time,
                   const Mat3 &contextTransform, float contextOpacity, int depth,
                   std::vector<EvaluatedLayer> &out) {
    if (!layer.visible) {
        return;
    }
    if (time < layer.inPoint || time >= layer.outPoint) {
        return;
    }
    std::vector<EntityId> visiting;
    const Mat3 world =
        WorldTransformOf(document, layer, time, contextTransform, visiting);
    visiting.clear();
    const float opacity = WorldOpacityOf(document, layer, time, contextOpacity, visiting);

    if (layer.type() == LayerType::Precomp) {
        if (depth >= kMaxPrecompDepth) {
            return;
        }
        const auto &precomp = static_cast<const PrecompContent &>(*layer.content);
        const Composition *source =
            document.entityIndex().findComposition(precomp.compositionId);
        if (!source) {
            return;
        }
        // innerTime = (outer - inPoint) * timeStretch + startTime
        const double inner =
            static_cast<double>(time - layer.inPoint) * layer.timeStretch +
            static_cast<double>(layer.startTime);
        EvaluateComposition(document, *source, static_cast<PreviewTime>(inner), world,
                            opacity, depth + 1, out);
        return;
    }
    if (layer.content->type() != LayerType::Shape) {
        return;  // M2: Null/Image/Text layers produce no draw items
    }
    const auto &shapeContent = static_cast<const ShapeContent &>(*layer.content);
    EvaluatedLayer evaluated;
    evaluated.id = layer.id;
    evaluated.worldTransform = world;
    evaluated.opacity = opacity;
    evaluated.blendMode = layer.blendMode;
    if (!layer.styles.empty()) {
        std::vector<BezierPath> paths;
        if (shapeContent.geometry) {
            CollectGeometryPath(*shapeContent.geometry, time, world, paths);
        }
        ApplyLayerStyles(layer, time, 1.0f, paths, evaluated.shapeItems);
    }
    if (!evaluated.shapeItems.empty()) {
        out.push_back(std::move(evaluated));
    }
}

void EvaluateComposition(const Document &document, const Composition &composition,
                         PreviewTime time, const Mat3 &contextTransform,
                         float contextOpacity, int depth,
                         std::vector<EvaluatedLayer> &out) {
    for (const auto &layer : composition.layers) {
        EvaluateLayer(document, *layer, time, contextTransform, contextOpacity, depth,
                      out);
    }
}

}  // namespace

Expected<SceneState, std::string> SceneEvaluator::Evaluate(const Document &document,
                                                           EntityId compositionId, FrameTime time) {
    return EvaluatePreview(document, compositionId, static_cast<PreviewTime>(time));
}

Expected<SceneState, std::string> SceneEvaluator::EvaluatePreview(const Document &document,
                                                                  EntityId compositionId,
                                                                  PreviewTime time) {
    const Composition *composition =
        document.entityIndex().findComposition(compositionId);
    if (!composition) {
        return Unexpected(std::string("composition not found"));
    }
    SceneState state;
    state.viewportWidth = composition->width;
    state.viewportHeight = composition->height;
    state.backgroundColor = composition->backgroundColor;
    state.cornerRadius = std::clamp(composition->cornerRadius, 0.0f,
                                    static_cast<float>(std::min(composition->width, composition->height)) * 0.5f);
    EvaluateComposition(document, *composition, time, Mat3::Identity(), 1.0f, 0,
                        state.layers);
    return state;
}

}  // namespace motion
