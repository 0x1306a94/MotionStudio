#include "MotionStudio/render/SceneEvaluator.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/PrecompContent.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapeFill.h"
#include "MotionStudio/model/ShapeGroup.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/ShapeStroke.h"

namespace motion {

namespace {

constexpr int kMaxPrecompDepth = 1024;
constexpr float kEllipseKappa = 0.5522847498f;

Mat3 LocalMatrixOf(const Transform &transform, FrameTime time) {
    return Mat3::Translate(transform.position.evaluate(time)) *
        Mat3::Rotate(transform.rotation.evaluate(time)) *
        Mat3::Scale(transform.scale.evaluate(time)) *
        Mat3::Translate(-transform.anchorPoint.evaluate(time));
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
BezierPath RectToPath(const ShapeRect &rect, FrameTime time) {
    const Vec2 center = rect.position.evaluate(time);
    const Vec2 size = rect.size.evaluate(time);
    const float halfWidth = std::max(size.x * 0.5f, 0.0f);
    const float halfHeight = std::max(size.y * 0.5f, 0.0f);
    const float radius = std::clamp(rect.cornerRadius.evaluate(time), 0.0f,
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
BezierPath EllipseToPath(const ShapeEllipse &ellipse, FrameTime time) {
    const Vec2 center = ellipse.position.evaluate(time);
    const Vec2 size = ellipse.size.evaluate(time);
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

// Flattens shape elements into world-space items. Paths accumulate until a
// Fill/Stroke consumes them; Groups recurse with a composed transform and
// compounded alpha.
void EvaluateElements(const std::vector<std::unique_ptr<ShapeElement>> &elements,
                      FrameTime time, const Mat3 &transform, float alpha,
                      std::vector<EvaluatedShapeItem> &items) {
    std::vector<BezierPath> localPaths;
    for (const auto &element : elements) {
        switch (element->type()) {
            case ShapeType::Path: {
                const auto &shape = static_cast<const ShapePath &>(*element);
                localPaths.push_back(shape.path.evaluate(time));
                break;
            }
            case ShapeType::Rect: {
                const auto &shape = static_cast<const ShapeRect &>(*element);
                localPaths.push_back(RectToPath(shape, time));
                break;
            }
            case ShapeType::Ellipse: {
                const auto &shape = static_cast<const ShapeEllipse &>(*element);
                localPaths.push_back(EllipseToPath(shape, time));
                break;
            }
            case ShapeType::Fill: {
                const auto &fill = static_cast<const ShapeFill &>(*element);
                Color color = fill.color.evaluate(time);
                color.a *= fill.opacity.evaluate(time) * alpha;
                const Paint paint{color, fill.fillRule};
                for (const BezierPath &local : localPaths) {
                    items.push_back({TransformPath(local, transform), paint, false, 0,
                                     LineCap::Butt, LineJoin::Miter});
                }
                break;
            }
            case ShapeType::Stroke: {
                const auto &stroke = static_cast<const ShapeStroke &>(*element);
                Color color = stroke.color.evaluate(time);
                color.a *= stroke.opacity.evaluate(time) * alpha;
                const Paint paint{color, FillRule::NonZero};
                const float width = stroke.width.evaluate(time);
                for (const BezierPath &local : localPaths) {
                    items.push_back({TransformPath(local, transform), paint, true, width,
                                     stroke.cap, stroke.join});
                }
                break;
            }
            case ShapeType::Group: {
                const auto &group = static_cast<const ShapeGroup &>(*element);
                const Mat3 childTransform =
                    transform * LocalMatrixOf(group.transform, time);
                const float childAlpha =
                    alpha * group.transform.opacity.evaluate(time);
                EvaluateElements(group.elements, time, childTransform, childAlpha,
                                 items);
                break;
            }
            case ShapeType::TrimPath: {
                break;  // M2: data only, trim expansion deferred
            }
        }
    }
}

// World transform with parent-chain walking; context is the transform handed
// down by the enclosing precomp. visiting guards against parent cycles.
Mat3 WorldTransformOf(const Document &document, const Layer &layer, FrameTime time,
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

float WorldOpacityOf(const Document &document, const Layer &layer, FrameTime time,
                     float context, std::vector<EntityId> &visiting) {
    for (const EntityId &visited : visiting) {
        if (visited == layer.id) {
            return context;
        }
    }
    visiting.push_back(layer.id);
    const float own = layer.transform.opacity.evaluate(time);
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
                         FrameTime time, const Mat3 &contextTransform,
                         float contextOpacity, int depth,
                         std::vector<EvaluatedLayer> &out);

void EvaluateLayer(const Document &document, const Layer &layer, FrameTime time,
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
            double(time - layer.inPoint) * layer.timeStretch + double(layer.startTime);
        EvaluateComposition(document, *source, FrameTime(std::llround(inner)), world,
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
    EvaluateElements(shapeContent.elements, time, world, 1.0f, evaluated.shapeItems);
    if (!evaluated.shapeItems.empty()) {
        out.push_back(std::move(evaluated));
    }
}

void EvaluateComposition(const Document &document, const Composition &composition,
                         FrameTime time, const Mat3 &contextTransform,
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
                                    float(std::min(composition->width, composition->height)) * 0.5f);
    EvaluateComposition(document, *composition, time, Mat3::Identity(), 1.0f, 0,
                        state.layers);
    return state;
}

}  // namespace motion
