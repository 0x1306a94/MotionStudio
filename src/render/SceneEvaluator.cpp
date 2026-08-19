#include "MotionStudio/render/SceneEvaluator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/BezierPathTransform.h"
#include "MotionStudio/common/VectorNetworkCompile.h"
#include "MotionStudio/model/Asset.h"
#include "MotionStudio/model/AssetType.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/GradientPaint.h"
#include "MotionStudio/model/GradientType.h"
#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerEffect.h"
#include "MotionStudio/model/LayerFx.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/LayerStylePaint.h"
#include "MotionStudio/model/NullContent.h"
#include "MotionStudio/model/PrecompContent.h"
#include "MotionStudio/model/ShaderUniformValues.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/StylePaintMode.h"
#include "MotionStudio/model/TextContent.h"
#include "MotionStudio/render/FollowPathEval.h"
#include "MotionStudio/render/Paint.h"
#include "MotionStudio/render/ShapeGeometry.h"

namespace motion {

namespace {

constexpr int kMaxPrecompDepth = 64;

std::string JoinProjectPath(const std::string &projectRoot, const std::string &relativePath) {
    if (projectRoot.empty() || relativePath.empty()) {
        return {};
    }
    if (projectRoot.back() == '/') {
        return projectRoot + relativePath;
    }
    return projectRoot + "/" + relativePath;
}

const Asset *FindAsset(const Document &document, EntityId assetId) {
    if (!assetId.isValid()) {
        return nullptr;
    }
    for (const Asset &asset : document.assets) {
        if (asset.id == assetId) {
            return &asset;
        }
    }
    return nullptr;
}

void FillCommonLayerFields(const Document &document, const Layer &layer, PreviewTime time,
                           const Mat3 &world, float opacity, EvaluatedLayer &evaluated) {
    evaluated.id = layer.id;
    evaluated.parentId = layer.parentId;
    evaluated.worldTransform = world;
    evaluated.worldAnchor = world.transformPoint(layer.transform.anchorPoint.evaluatePreview(time));
    evaluated.opacity = opacity;
    evaluated.blendMode = layer.blendMode;
    for (const Mask &mask : layer.masks) {
        EvaluatedMask evaluatedMask;
        const VectorNetwork network = mask.path.evaluatePreview(time);
        evaluatedMask.network = network;
        evaluatedMask.path = CompileFillFaces(network);
        evaluatedMask.mode = mask.mode;
        evaluatedMask.opacity = mask.opacity.evaluatePreview(time);
        evaluatedMask.inverted = mask.inverted;
        evaluatedMask.feather = mask.feather.evaluatePreview(time);
        evaluatedMask.expansion = mask.expansion.evaluatePreview(time);
        evaluated.masks.push_back(std::move(evaluatedMask));
    }
    if (layer.trackMatteType != TrackMatteType::None && layer.trackMatteLayerId.isValid() &&
        layer.trackMatteLayerId != layer.id) {
        if (document.entityIndex().findLayer(layer.trackMatteLayerId) != nullptr) {
            evaluated.trackMatteType = layer.trackMatteType;
            evaluated.matteSourceId = layer.trackMatteLayerId;
        }
    }
    for (const auto &effect : layer.effects) {
        if (std::shared_ptr<const LayerEffect> snap = effect->snapshot(time)) {
            evaluated.effects.push_back(std::move(snap));
        }
    }
    for (const auto &style : layer.layerStyles) {
        if (std::shared_ptr<const LayerFx> snap = style->snapshot(time)) {
            evaluated.layerStyles.push_back(std::move(snap));
        }
    }
}

void CollectGeometry(const ShapeElement &element, PreviewTime time,
                     std::vector<ShapeGeometry> &geometries) {
    switch (element.type()) {
        case ShapeType::Path: {
            const auto &shape = static_cast<const ShapePath &>(element);
            const VectorNetwork network = shape.path.evaluatePreview(time);
            ShapeGeometry geometry = MakePathGeometry(CompileFillFaces(network));
            geometry.strokePath = CompileStrokeEdges(network);
            geometries.push_back(std::move(geometry));
            break;
        }
        case ShapeType::Rect: {
            const auto &shape = static_cast<const ShapeRect &>(element);
            const Vec2 center = shape.position.evaluatePreview(time);
            const Vec2 size = shape.size.evaluatePreview(time);
            const float halfWidth = std::max(size.x * 0.5f, 0.0f);
            const float halfHeight = std::max(size.y * 0.5f, 0.0f);
            const float cornerRadius = std::clamp(shape.cornerRadius.evaluatePreview(time), 0.0f,
                                                  std::min(halfWidth, halfHeight));
            geometries.push_back(MakeRectGeometry(center, size, cornerRadius));
            break;
        }
        case ShapeType::Ellipse: {
            const auto &shape = static_cast<const ShapeEllipse &>(element);
            geometries.push_back(MakeEllipseGeometry(shape.position.evaluatePreview(time),
                                                     shape.size.evaluatePreview(time)));
            break;
        }
        case ShapeType::TrimPath: {
            break;
        }
    }
}

std::vector<EvaluatedShaderUniform> EvaluateUniformValues(const ShaderUniformValues &values,
                                                          PreviewTime time) {
    std::vector<EvaluatedShaderUniform> out;
    out.reserve(values.entries.size());
    for (const ShaderUniformValue &entry : values.entries) {
        EvaluatedShaderUniform evaluated;
        evaluated.name = entry.name;
        evaluated.kind = entry.kind;
        switch (entry.kind) {
            case ShaderUniformValueKind::AnimFloat:
                evaluated.floatValue = entry.floatValue.evaluatePreview(time);
                break;
            case ShaderUniformValueKind::AnimFloat2:
                evaluated.float2Value = entry.float2Value.evaluatePreview(time);
                break;
            case ShaderUniformValueKind::AnimFloat3:
                evaluated.float3Value = entry.float3Value.evaluatePreview(time);
                break;
            case ShaderUniformValueKind::AnimFloat4:
                evaluated.float4Value = entry.float4Value.evaluatePreview(time);
                break;
            case ShaderUniformValueKind::AnimColor:
                evaluated.colorValue = entry.colorValue.evaluatePreview(time);
                break;
            default:
                break;
        }
        out.push_back(std::move(evaluated));
    }
    return out;
}

bool MakeShaderPaint(const Document &document, EntityId shaderId,
                     const ShaderUniformValues &uniformValues, PreviewTime time,
                     ShaderPaint &out) {
    const ShaderDefinition *def = FindShader(document, shaderId);
    if (def == nullptr) {
        return false;
    }
    out.shaderId = def->id;
    out.mainImage = def->mainImage;
    out.uniforms = def->uniforms;
    out.values = EvaluateUniformValues(uniformValues, time);
    return true;
}

bool BoundsOfShapeGeometries(const std::vector<ShapeGeometry> &geometries, Vec2 &outMin,
                             Vec2 &outMax) {
    Vec2 minPoint{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec2 maxPoint{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    bool hasBounds = false;
    for (const ShapeGeometry &geometry : geometries) {
        switch (geometry.kind) {
            case ShapeGeometryKind::Rect:
            case ShapeGeometryKind::Ellipse: {
                const float halfW = std::max(geometry.size.x * 0.5f, 0.f);
                const float halfH = std::max(geometry.size.y * 0.5f, 0.f);
                const Vec2 minCorner{geometry.center.x - halfW, geometry.center.y - halfH};
                const Vec2 maxCorner{geometry.center.x + halfW, geometry.center.y + halfH};
                minPoint.x = std::min(minPoint.x, minCorner.x);
                minPoint.y = std::min(minPoint.y, minCorner.y);
                maxPoint.x = std::max(maxPoint.x, maxCorner.x);
                maxPoint.y = std::max(maxPoint.y, maxCorner.y);
                hasBounds = true;
                break;
            }
            case ShapeGeometryKind::Path: {
                for (const BezierPath::Contour &contour : geometry.path.contours) {
                    for (const BezierPath::Vertex &vertex : contour.vertices) {
                        minPoint.x = std::min(minPoint.x, vertex.point.x);
                        minPoint.y = std::min(minPoint.y, vertex.point.y);
                        maxPoint.x = std::max(maxPoint.x, vertex.point.x);
                        maxPoint.y = std::max(maxPoint.y, vertex.point.y);
                        hasBounds = true;
                    }
                }
                break;
            }
        }
    }
    if (!hasBounds) {
        return false;
    }
    outMin = minPoint;
    outMax = maxPoint;
    return true;
}

bool MakeGradientPaint(const GradientPaint &src, PreviewTime time, Vec2 aabbOrigin,
                       EvaluatedGradient &out) {
    if (!GradientStopsAreValid(src)) {
        return false;
    }
    out.type = src.type;
    // Stored coords are AABB top-left relative; draw space is shape-path local.
    out.start = src.start.evaluatePreview(time) + aabbOrigin;
    out.end = src.end.evaluatePreview(time) + aabbOrigin;
    out.startAngle = src.startAngle.evaluatePreview(time);
    out.endAngle = src.endAngle.evaluatePreview(time);
    if (src.type == GradientType::Radial || src.type == GradientType::Diamond) {
        const float dx = out.end.x - out.start.x;
        const float dy = out.end.y - out.start.y;
        if (dx * dx + dy * dy <= 0.f) {
            return false;
        }
    }
    out.stops.clear();
    out.stops.reserve(src.stops.size());
    for (const GradientStop &stop : src.stops) {
        EvaluatedGradientStop evaluated;
        evaluated.color = stop.color.evaluatePreview(time);
        evaluated.position = stop.position.evaluatePreview(time);
        out.stops.push_back(evaluated);
    }
    return true;
}

bool ResolveStylePaint(const Document &document, StylePaintMode paintMode,
                       const Animatable<Color> &color, const GradientPaint &gradient,
                       EntityId shaderId, const ShaderUniformValues &uniformValues,
                       PreviewTime time, Vec2 gradientOrigin, Paint &paint) {
    if (paintMode == StylePaintMode::Shader) {
        if (!MakeShaderPaint(document, shaderId, uniformValues, time, paint.shader)) {
            return false;
        }
        paint.paintMode = StylePaintMode::Shader;
        return true;
    }
    if (paintMode == StylePaintMode::Gradient) {
        if (!MakeGradientPaint(gradient, time, gradientOrigin, paint.gradient)) {
            return false;
        }
        paint.paintMode = StylePaintMode::Gradient;
        return true;
    }
    paint.paintMode = StylePaintMode::Color;
    paint.color = color.evaluatePreview(time);
    return true;
}

void ApplyLayerStyles(const Document &document, const Layer &layer, PreviewTime time, float alpha,
                      const std::vector<ShapeGeometry> &geometries,
                      std::vector<EvaluatedShapeItem> &items) {
    Vec2 gradientOrigin{0.f, 0.f};
    Vec2 gradientMax{};
    if (!BoundsOfShapeGeometries(geometries, gradientOrigin, gradientMax)) {
        gradientOrigin = {0.f, 0.f};
    }

    // Paint order is Fill block then Stroke block (stable within each type),
    // independent of styles[] interleaving on disk.
    auto appendFill = [&](const FillStyle &fill, int styleIndex) {
        Paint paint;
        paint.alpha = alpha;
        paint.fillRule = fill.fillRule;
        paint.blendMode = fill.blendMode;
        if (!ResolveStylePaint(document, fill.paintMode, fill.color, fill.gradient, fill.shaderId,
                               fill.uniformValues, time, gradientOrigin, paint)) {
            return;
        }
        for (const ShapeGeometry &geometry : geometries) {
            EvaluatedShapeItem item;
            item.geometry = geometry;
            item.paint = paint;
            item.isStroke = false;
            item.styleIndex = styleIndex;
            items.push_back(std::move(item));
        }
    };
    auto appendStroke = [&](const StrokeStyle &stroke, int styleIndex) {
        Paint paint;
        paint.alpha = alpha;
        paint.fillRule = FillRule::NonZero;
        paint.blendMode = stroke.blendMode;
        if (!ResolveStylePaint(document, stroke.paintMode, stroke.color, stroke.gradient,
                               stroke.shaderId, stroke.uniformValues, time, gradientOrigin,
                               paint)) {
            return;
        }
        const StrokeOptions options{stroke.width.evaluatePreview(time),
                                    stroke.cap,
                                    stroke.join,
                                    stroke.position,
                                    stroke.trimStart.evaluatePreview(time),
                                    stroke.trimEnd.evaluatePreview(time),
                                    stroke.trimOffset.evaluatePreview(time)};
        for (const ShapeGeometry &geometry : geometries) {
            // Stroke uses strokePath when present; fill faces stay on
            // geometry.path for Inside/Outside positioning.
            EvaluatedShapeItem item;
            item.geometry = geometry;
            item.paint = paint;
            item.isStroke = true;
            item.stroke = options;
            item.styleIndex = styleIndex;
            items.push_back(std::move(item));
        }
    };
    for (size_t index = 0; index < layer.styles.size(); ++index) {
        const auto &style = layer.styles[index];
        if (style->type() == LayerStyleType::Fill) {
            appendFill(static_cast<const FillStyle &>(*style), static_cast<int>(index));
        }
    }
    for (size_t index = 0; index < layer.styles.size(); ++index) {
        const auto &style = layer.styles[index];
        if (style->type() == LayerStyleType::Stroke) {
            appendStroke(static_cast<const StrokeStyle &>(*style), static_cast<int>(index));
        }
    }
}

// World transform with parent-chain walking; context is the transform handed
// down by the enclosing precomp. visiting guards against parent cycles.
Mat3 WorldTransformOf(const Document &document, const Layer &layer, PreviewTime time,
                      const Mat3 &context, std::vector<EntityId> &visiting) {
    std::vector<EntityId> followVisiting;
    return FollowAwareWorldTransform(document, layer, time, context, visiting, followVisiting);
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
    if (layer.type() == LayerType::Precomp && depth >= kMaxPrecompDepth) {
        return;
    }
    if (!layer.isEffectivelyVisible(document)) {
        return;
    }
    if (time < layer.inPoint || time >= layer.outPoint) {
        return;
    }
    std::vector<EntityId> visiting;
    const Mat3 world = WorldTransformOf(document, layer, time, contextTransform, visiting);
    visiting.clear();
    const float opacity = WorldOpacityOf(document, layer, time, contextOpacity, visiting);

    if (layer.type() == LayerType::Precomp) {
        const auto &precomp = static_cast<const PrecompContent &>(*layer.content);
        const Composition *source = document.entityIndex().findComposition(precomp.compositionId);
        if (!source) {
            return;
        }
        for (const EvaluatedLayer &existing : out) {
            if (existing.id == layer.id) {
                return;
            }
        }
        EvaluatedLayer stub;
        FillCommonLayerFields(document, layer, time, world, opacity, stub);
        out.push_back(std::move(stub));
        const size_t innerBegin = out.size();
        const double inner =
            static_cast<double>(time - layer.inPoint) * layer.timeStretch +
            static_cast<double>(layer.startTime);
        EvaluateComposition(document, *source, static_cast<PreviewTime>(inner), world, opacity, depth + 1,
                            out);
        for (size_t index = innerBegin; index < out.size(); ++index) {
            if (!out[index].parentId.isValid()) {
                out[index].parentId = layer.id;
            }
        }
        return;
    }
    if (layer.content->type() == LayerType::Image) {
        const auto &imageContent = static_cast<const ImageContent &>(*layer.content);
        EvaluatedLayer evaluated;
        FillCommonLayerFields(document, layer, time, world, opacity, evaluated);
        EvaluatedImageItem imageItem;
        imageItem.assetId = imageContent.assetId;
        imageItem.containerSize = imageContent.size.evaluatePreview(time);
        imageItem.scaleMode = imageContent.scaleMode;
        if (const Asset *asset = FindAsset(document, imageContent.assetId)) {
            imageItem.intrinsicSize = {static_cast<float>(asset->width), static_cast<float>(asset->height)};
            imageItem.absolutePath = JoinProjectPath(document.projectRoot, asset->path);
        }
        const float radius =
            ClampCornerRadius(imageContent.cornerRadius.evaluatePreview(time), imageItem.containerSize);
        imageItem.cornerRadius = radius;
        evaluated.cornerRadius = radius;
        evaluated.imageItem = std::move(imageItem);
        out.push_back(std::move(evaluated));
        return;
    }
    if (layer.content->type() == LayerType::Text) {
        const auto &textContent = static_cast<const TextContent &>(*layer.content);
        EvaluatedLayer evaluated;
        FillCommonLayerFields(document, layer, time, world, opacity, evaluated);
        EvaluatedTextItem textItem;
        textItem.text = textContent.text.evaluatePreview(time);
        textItem.fontSize = textContent.fontSize;
        textItem.containerSize = textContent.size;
        textItem.boxTextMode = textContent.boxTextMode;
        textItem.align = textContent.align;
        textItem.fontFamily = textContent.fontFamily;
        textItem.fontStyle = textContent.fontStyle;
        for (const auto &style : layer.styles) {
            if (style->type() != LayerStyleType::Fill) {
                continue;
            }
            const auto &fill = static_cast<const FillStyle &>(*style);
            // Text draw path is solid-color only in v1; skip shader paints.
            if (fill.paintMode == StylePaintMode::Shader) {
                continue;
            }
            TextDrawStyle paint;
            paint.color = fill.color.evaluatePreview(time);
            paint.blendMode = fill.blendMode;
            paint.isStroke = false;
            textItem.styles.push_back(paint);
        }
        for (const auto &style : layer.styles) {
            if (style->type() != LayerStyleType::Stroke) {
                continue;
            }
            const auto &stroke = static_cast<const StrokeStyle &>(*style);
            if (stroke.paintMode == StylePaintMode::Shader) {
                continue;
            }
            const float width = stroke.width.evaluatePreview(time);
            if (width <= 0.0f) {
                continue;
            }
            TextDrawStyle paint;
            paint.color = stroke.color.evaluatePreview(time);
            paint.blendMode = stroke.blendMode;
            paint.isStroke = true;
            paint.strokeWidth = width;
            textItem.styles.push_back(paint);
        }
        if (textItem.styles.empty()) {
            TextDrawStyle paint;
            paint.color = Color{0, 0, 0, 1};
            textItem.styles.push_back(paint);
        }
        const TextPath &textPath = textContent.textPath;
        if (textPath.enabled && textPath.pathLayerId.isValid() &&
            textPath.pathLayerId != layer.id) {
            const Layer *pathLayer = document.entityIndex().findLayer(textPath.pathLayerId);
            if (pathLayer != nullptr) {
                const std::optional<BezierPath> optPath = EvaluateLayerPath(*pathLayer, time);
                if (optPath) {
                    std::vector<EntityId> pathParentVisiting;
                    std::vector<EntityId> pathFollowVisiting;
                    const Mat3 pathWorld = FollowAwareWorldTransform(document, *pathLayer, time, contextTransform, pathParentVisiting, pathFollowVisiting);
                    Mat3 textInverse = Mat3::Identity();
                    if (world.tryInvert(textInverse)) {
                        EvaluatedTextPath evaluatedPath;
                        evaluatedPath.path = TransformBezierPath(*optPath, textInverse * pathWorld);
                        evaluatedPath.reversed = textPath.reversed;
                        evaluatedPath.perpendicular = textPath.perpendicular;
                        evaluatedPath.forceAlignment = textPath.forceAlignment;
                        evaluatedPath.firstMargin = textPath.firstMargin.evaluatePreview(time);
                        evaluatedPath.lastMargin = textPath.lastMargin.evaluatePreview(time);
                        textItem.textPath = std::move(evaluatedPath);
                    }
                }
            }
        }
        evaluated.textItem = std::move(textItem);
        out.push_back(std::move(evaluated));
        return;
    }
    if (layer.content->type() == LayerType::Group) {
        EvaluatedLayer evaluated;
        FillCommonLayerFields(document, layer, time, world, opacity, evaluated);
        const auto &nullContent = static_cast<const NullContent &>(*layer.content);
        evaluated.cornerRadius = std::max(nullContent.cornerRadius.evaluatePreview(time), 0.0f);
        out.push_back(std::move(evaluated));
        return;
    }
    if (layer.content->type() != LayerType::Shape) {
        return;
    }
    const auto &shapeContent = static_cast<const ShapeContent &>(*layer.content);
    EvaluatedLayer evaluated;
    FillCommonLayerFields(document, layer, time, world, opacity, evaluated);
    if (shapeContent.geometry != nullptr && shapeContent.geometry->type() == ShapeType::Path) {
        const auto &shape = static_cast<const ShapePath &>(*shapeContent.geometry);
        evaluated.shapeNetwork = shape.path.evaluatePreview(time);
    }
    if (!layer.styles.empty()) {
        std::vector<ShapeGeometry> geometries;
        if (shapeContent.geometry) {
            CollectGeometry(*shapeContent.geometry, time, geometries);
        }
        ApplyLayerStyles(document, layer, time, 1.0f, geometries, evaluated.shapeItems);
    }
    // Path networks without styles still need an evaluated layer for path edit.
    if (!evaluated.shapeItems.empty() || !evaluated.shapeNetwork.vertices.empty()) {
        out.push_back(std::move(evaluated));
    }
}

void MarkMatteSources(std::vector<EvaluatedLayer> &layers) {
    std::unordered_set<EntityId> matteSourceIds;
    for (const EvaluatedLayer &layer : layers) {
        if (layer.trackMatteType != TrackMatteType::None && layer.matteSourceId.isValid()) {
            matteSourceIds.insert(layer.matteSourceId);
        }
    }
    if (matteSourceIds.empty()) {
        return;
    }
    for (EvaluatedLayer &layer : layers) {
        EntityId currentId = layer.id;
        std::unordered_set<EntityId> visiting;
        while (currentId.isValid()) {
            if (matteSourceIds.find(currentId) != matteSourceIds.end()) {
                layer.usedAsMatteOnly = true;
                break;
            }
            if (!visiting.insert(currentId).second) {
                break;
            }
            const EvaluatedLayer *current = nullptr;
            for (const EvaluatedLayer &candidate : layers) {
                if (candidate.id == currentId) {
                    current = &candidate;
                    break;
                }
            }
            if (current == nullptr || !current->parentId.isValid()) {
                break;
            }
            currentId = current->parentId;
        }
    }
}

void EvaluateComposition(const Document &document, const Composition &composition,
                         PreviewTime time, const Mat3 &contextTransform,
                         float contextOpacity, int depth,
                         std::vector<EvaluatedLayer> &out) {
    for (const auto &layer : composition.layers) {
        EvaluateLayer(document, *layer, time, contextTransform, contextOpacity, depth, out);
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
    const Composition *composition = document.entityIndex().findComposition(compositionId);
    if (!composition) {
        return Unexpected(std::string("composition not found"));
    }
    SceneState state;
    state.viewportWidth = composition->width;
    state.viewportHeight = composition->height;
    state.backgroundColor = composition->backgroundColor;
    state.cornerRadius = std::clamp(composition->cornerRadius, 0.0f, static_cast<float>(std::min(composition->width, composition->height)) * 0.5f);
    const float fps = composition->frameRate.num / static_cast<float>(std::max(composition->frameRate.den, 1u));
    state.frameRate = fps;
    state.frameIndex = static_cast<int64_t>(time);
    state.timeSeconds = fps > 0.f ? static_cast<float>(time / static_cast<double>(fps)) : 0.f;
    EvaluateComposition(document, *composition, time, Mat3::Identity(), 1.0f, 0, state.layers);
    MarkMatteSources(state.layers);
    return state;
}

}  // namespace motion
