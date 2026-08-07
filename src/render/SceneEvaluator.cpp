#include "MotionStudio/render/SceneEvaluator.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "MotionStudio/common/BezierPathTransform.h"
#include "MotionStudio/common/VectorNetworkCompile.h"
#include "MotionStudio/model/Asset.h"
#include "MotionStudio/model/AssetType.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/LayerStyle.h"
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

constexpr int kMaxPrecompDepth = 1024;

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
                     const ShaderUniformValues &uniformValues, PreviewTime time, float alpha,
                     ShaderPaint &out) {
    const ShaderDefinition *def = FindShader(document, shaderId);
    if (def == nullptr) {
        return false;
    }
    out.shaderId = def->id;
    out.mainImage = def->mainImage;
    out.uniforms = def->uniforms;
    out.values = EvaluateUniformValues(uniformValues, time);
    for (EvaluatedShaderUniform &value : out.values) {
        if (value.kind == ShaderUniformValueKind::AnimColor) {
            value.colorValue.a *= alpha;
        }
    }
    return true;
}

void ApplyLayerStyles(const Document &document, const Layer &layer, PreviewTime time, float alpha,
                      const std::vector<ShapeGeometry> &geometries,
                      std::vector<EvaluatedShapeItem> &items) {
    for (const auto &style : layer.styles) {
        switch (style->type()) {
            case LayerStyleType::Fill: {
                const auto &fill = static_cast<const FillStyle &>(*style);
                Paint paint;
                paint.fillRule = fill.fillRule;
                paint.blendMode = fill.blendMode;
                if (fill.paintMode == StylePaintMode::Shader) {
                    if (!MakeShaderPaint(document, fill.shaderId, fill.uniformValues, time, alpha, paint.shader)) {
                        break;
                    }
                    paint.paintMode = StylePaintMode::Shader;
                } else {
                    paint.paintMode = StylePaintMode::Color;
                    paint.color = fill.color.evaluatePreview(time);
                    paint.color.a *= alpha;
                }
                for (const ShapeGeometry &geometry : geometries) {
                    items.push_back({geometry, paint, false, {}});
                }
                break;
            }
            case LayerStyleType::Stroke: {
                const auto &stroke = static_cast<const StrokeStyle &>(*style);
                Paint paint;
                paint.fillRule = FillRule::NonZero;
                paint.blendMode = stroke.blendMode;
                if (stroke.paintMode == StylePaintMode::Shader) {
                    if (!MakeShaderPaint(document, stroke.shaderId, stroke.uniformValues, time, alpha, paint.shader)) {
                        break;
                    }
                    paint.paintMode = StylePaintMode::Shader;
                } else {
                    paint.paintMode = StylePaintMode::Color;
                    paint.color = stroke.color.evaluatePreview(time);
                    paint.color.a *= alpha;
                }
                const StrokeOptions options{
                    stroke.width.evaluatePreview(time),
                    stroke.cap,
                    stroke.join,
                    stroke.position,
                    stroke.trimStart.evaluatePreview(time),
                    stroke.trimEnd.evaluatePreview(time),
                    stroke.trimOffset.evaluatePreview(time)};
                for (const ShapeGeometry &geometry : geometries) {
                    // Stroke uses strokePath when present; fill faces stay on
                    // geometry.path for Inside/Outside positioning.
                    items.push_back({geometry, paint, true, options});
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
    if (!layer.visible) {
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
        if (depth >= kMaxPrecompDepth) {
            return;
        }
        const auto &precomp = static_cast<const PrecompContent &>(*layer.content);
        const Composition *source = document.entityIndex().findComposition(precomp.compositionId);
        if (!source) {
            return;
        }
        // innerTime = (outer - inPoint) * timeStretch + startTime
        const double inner =
            static_cast<double>(time - layer.inPoint) * layer.timeStretch +
            static_cast<double>(layer.startTime);
        EvaluateComposition(document, *source, static_cast<PreviewTime>(inner), world, opacity, depth + 1, out);
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
            if (style->type() == LayerStyleType::Fill) {
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
            } else if (style->type() == LayerStyleType::Stroke) {
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
    if (layer.content->type() != LayerType::Shape) {
        return;  // Group layers produce no draw items
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
    std::vector<EntityId> matteSourceIds;
    for (const EvaluatedLayer &layer : layers) {
        if (layer.trackMatteType != TrackMatteType::None && layer.matteSourceId.isValid()) {
            matteSourceIds.push_back(layer.matteSourceId);
        }
    }
    for (EvaluatedLayer &layer : layers) {
        for (const EntityId &sourceId : matteSourceIds) {
            if (layer.id == sourceId) {
                layer.usedAsMatteOnly = true;
                break;
            }
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
