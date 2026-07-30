#include "MotionStudio/render/SceneEvaluator.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "MotionStudio/model/Asset.h"
#include "MotionStudio/model/AssetType.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/PrecompContent.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/TextContent.h"
#include "MotionStudio/render/FollowPathEval.h"
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
    evaluated.worldAnchor =
        world.transformPoint(layer.transform.anchorPoint.evaluatePreview(time));
    evaluated.opacity = opacity;
    evaluated.blendMode = layer.blendMode;
    for (const Mask &mask : layer.masks) {
        EvaluatedMask evaluatedMask;
        evaluatedMask.path = mask.path.evaluatePreview(time);
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
            geometries.push_back(MakePathGeometry(shape.path.evaluatePreview(time)));
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

void ApplyLayerStyles(const Layer &layer, PreviewTime time, float alpha,
                      const std::vector<ShapeGeometry> &geometries,
                      std::vector<EvaluatedShapeItem> &items) {
    for (const auto &style : layer.styles) {
        switch (style->type()) {
            case LayerStyleType::Fill: {
                const auto &fill = static_cast<const FillStyle &>(*style);
                Color color = fill.color.evaluatePreview(time);
                color.a *= alpha;
                const Paint paint{color, fill.fillRule, fill.blendMode};
                for (const ShapeGeometry &geometry : geometries) {
                    items.push_back({geometry, paint, false, {}});
                }
                break;
            }
            case LayerStyleType::Stroke: {
                const auto &stroke = static_cast<const StrokeStyle &>(*style);
                Color color = stroke.color.evaluatePreview(time);
                color.a *= alpha;
                const Paint paint{color, FillRule::NonZero, stroke.blendMode};
                const StrokeOptions options{stroke.width.evaluatePreview(time),
                                            stroke.cap,
                                            stroke.join,
                                            stroke.position,
                                            stroke.trimStart.evaluatePreview(time),
                                            stroke.trimEnd.evaluatePreview(time),
                                            stroke.trimOffset.evaluatePreview(time)};
                for (const ShapeGeometry &geometry : geometries) {
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
    if (layer.content->type() == LayerType::Image) {
        const auto &imageContent = static_cast<const ImageContent &>(*layer.content);
        EvaluatedLayer evaluated;
        FillCommonLayerFields(document, layer, time, world, opacity, evaluated);
        EvaluatedImageItem imageItem;
        imageItem.assetId = imageContent.assetId;
        imageItem.containerSize = imageContent.size.evaluatePreview(time);
        imageItem.scaleMode = imageContent.scaleMode;
        if (const Asset *asset = FindAsset(document, imageContent.assetId)) {
            imageItem.intrinsicSize = {static_cast<float>(asset->width),
                                       static_cast<float>(asset->height)};
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
        textItem.fontSize = textContent.fontSize.evaluatePreview(time);
        textItem.containerSize = textContent.size.evaluatePreview(time);
        textItem.autoHeight = textContent.autoHeight;
        textItem.align = textContent.align;
        textItem.fontAssetId = textContent.fontAssetId;
        textItem.fontFamily = textContent.fontFamily;
        textItem.hitSize = textItem.containerSize;
        textItem.fillColor = Color{0, 0, 0, 1};
        if (const Asset *asset = FindAsset(document, textContent.fontAssetId)) {
            if (asset->type == AssetType::Font) {
                textItem.fontAbsolutePath = JoinProjectPath(document.projectRoot, asset->path);
            }
        }
        bool haveFill = false;
        for (const auto &style : layer.styles) {
            if (!haveFill && style->type() == LayerStyleType::Fill) {
                const auto &fill = static_cast<const FillStyle &>(*style);
                textItem.fillColor = fill.color.evaluatePreview(time);
                haveFill = true;
            } else if (!textItem.strokeColor.has_value() && style->type() == LayerStyleType::Stroke) {
                const auto &stroke = static_cast<const StrokeStyle &>(*style);
                const float width = stroke.width.evaluatePreview(time);
                if (width > 0.0f) {
                    textItem.strokeColor = stroke.color.evaluatePreview(time);
                    textItem.strokeWidth = width;
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
    if (!layer.styles.empty()) {
        std::vector<ShapeGeometry> geometries;
        if (shapeContent.geometry) {
            CollectGeometry(*shapeContent.geometry, time, geometries);
        }
        ApplyLayerStyles(layer, time, 1.0f, geometries, evaluated.shapeItems);
    }
    if (!evaluated.shapeItems.empty()) {
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
    MarkMatteSources(state.layers);
    return state;
}

}  // namespace motion
