#include "BridgeInternals.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "MeasurePointTextSize.h"
#include "MeasureTextPathBounds.h"

#include "MotionStudio/common/VectorNetworkCompile.h"
#include "MotionStudio/common/VectorNetworkConvert.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/render/MaskPathBake.h"
#include "MotionStudio/undo/AddLayerCommand.h"
#include "MotionStudio/undo/UndoManager.h"

using motion::Animatable;
using motion::AnimatableBase;
using motion::AnimatableType;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::Easing;
using motion::EntityId;
using motion::FillStyle;
using motion::FrameTime;
using motion::Keyframe;
using motion::Layer;
using motion::LayerType;
using motion::PropertyPath;
using motion::ResolveAnimatable;
using motion::ShapeContent;
using motion::ShapeEllipse;
using motion::ShapePath;
using motion::ShapeRect;
using motion::StrokeStyle;
using motion::UndoManager;
using motion::Vec2;

namespace bridge {

Document *Doc(MSDocument *handle) {
    return handle != nullptr ? handle->document.get() : nullptr;
}

Composition *FindComposition(MSDocument *handle, uint64_t compositionId) {
    Document *document = Doc(handle);
    if (document == nullptr) {
        return nullptr;
    }
    for (auto &composition : document->compositions) {
        if (composition->id.value == compositionId) {
            return composition.get();
        }
    }
    return nullptr;
}

Layer *FindLayer(MSDocument *handle, uint64_t layerId) {
    Document *document = Doc(handle);
    if (document == nullptr) {
        return nullptr;
    }
    return document->entityIndex().findLayer(EntityId{layerId});
}

AnimatableBase *FindProperty(MSDocument *handle, uint64_t entityId, const char *path) {
    Document *document = Doc(handle);
    if (document == nullptr || path == nullptr) {
        return nullptr;
    }
    return ResolveAnimatable(*document, PropertyPath{EntityId{entityId}, path});
}

const Animatable<float> *AsFloat(AnimatableBase *base) {
    if (base == nullptr || base->valueType() != AnimatableType::Float) {
        return nullptr;
    }
    return static_cast<const Animatable<float> *>(base);
}

const Animatable<Vec2> *AsVec2(AnimatableBase *base) {
    if (base == nullptr || base->valueType() != AnimatableType::Vec2) {
        return nullptr;
    }
    return static_cast<const Animatable<Vec2> *>(base);
}

const Animatable<Color> *AsColor(AnimatableBase *base) {
    if (base == nullptr || base->valueType() != AnimatableType::Color) {
        return nullptr;
    }
    return static_cast<const Animatable<Color> *>(base);
}

const Animatable<motion::VectorNetwork> *AsVectorNetwork(AnimatableBase *base) {
    if (base == nullptr || base->valueType() != AnimatableType::VectorNetwork) {
        return nullptr;
    }
    return static_cast<const Animatable<motion::VectorNetwork> *>(base);
}

MSBezierPath *AllocateMSBezierPath(const motion::BezierPath &path) {
    auto *result = static_cast<MSBezierPath *>(std::malloc(sizeof(MSBezierPath)));
    if (result == nullptr) {
        return nullptr;
    }
    const motion::BezierPath::Contour *contour = motion::PrimaryContour(path);
    result->count = contour == nullptr ? 0 : contour->vertices.size();
    result->closed = contour != nullptr && contour->closed;
    result->vertices = nullptr;
    if (result->count > 0) {
        result->vertices =
            static_cast<MSBezierVertex *>(std::calloc(result->count, sizeof(MSBezierVertex)));
        if (result->vertices == nullptr) {
            std::free(result);
            return nullptr;
        }
        for (size_t index = 0; index < result->count; ++index) {
            const motion::BezierPath::Vertex &vertex = contour->vertices[index];
            result->vertices[index].pointX = vertex.point.x;
            result->vertices[index].pointY = vertex.point.y;
            result->vertices[index].inTangentX = vertex.inTangent.x;
            result->vertices[index].inTangentY = vertex.inTangent.y;
            result->vertices[index].outTangentX = vertex.outTangent.x;
            result->vertices[index].outTangentY = vertex.outTangent.y;
        }
    }
    return result;
}

motion::BezierPath FromMSBezierPath(const MSBezierPath *path) {
    if (path == nullptr || path->vertices == nullptr || path->count == 0) {
        return {};
    }
    std::vector<motion::BezierPath::Vertex> vertices;
    vertices.reserve(path->count);
    for (size_t index = 0; index < path->count; ++index) {
        const MSBezierVertex &vertex = path->vertices[index];
        vertices.push_back({{vertex.pointX, vertex.pointY},
                            {vertex.inTangentX, vertex.inTangentY},
                            {vertex.outTangentX, vertex.outTangentY}});
    }
    return motion::MakeSingleContour(std::move(vertices), path->closed);
}

motion::BezierPath BridgePathFromNetwork(const motion::VectorNetwork &network) {
    motion::BezierPath path = motion::VectorNetworkToSingleRingBezierPath(network);
    if (!path.contours.empty()) {
        return path;
    }
    path = motion::CompileFillFaces(network);
    if (!path.contours.empty()) {
        return path;
    }
    return motion::CompileStrokeEdges(network);
}

motion::VectorNetwork BridgeNetworkFromPath(const motion::BezierPath &path) {
    return motion::BezierPathToVectorNetwork(path);
}

MSVectorNetwork *AllocateMSVectorNetwork(const motion::VectorNetwork &network) {
    auto *result = static_cast<MSVectorNetwork *>(std::malloc(sizeof(MSVectorNetwork)));
    if (result == nullptr) {
        return nullptr;
    }
    result->vertices = nullptr;
    result->vertexCount = network.vertices.size();
    result->edges = nullptr;
    result->edgeCount = network.edges.size();
    if (result->vertexCount > 0) {
        result->vertices = static_cast<MSVectorNetworkVertex *>(
            std::calloc(result->vertexCount, sizeof(MSVectorNetworkVertex)));
        if (result->vertices == nullptr) {
            std::free(result);
            return nullptr;
        }
        for (size_t index = 0; index < result->vertexCount; ++index) {
            const motion::VectorNetwork::Vertex &vertex = network.vertices[index];
            result->vertices[index].id = vertex.id;
            result->vertices[index].x = vertex.point.x;
            result->vertices[index].y = vertex.point.y;
        }
    }
    if (result->edgeCount > 0) {
        result->edges = static_cast<MSVectorNetworkEdge *>(
            std::calloc(result->edgeCount, sizeof(MSVectorNetworkEdge)));
        if (result->edges == nullptr) {
            std::free(result->vertices);
            std::free(result);
            return nullptr;
        }
        for (size_t index = 0; index < result->edgeCount; ++index) {
            const motion::VectorNetwork::Edge &edge = network.edges[index];
            result->edges[index].id = edge.id;
            result->edges[index].start = edge.start;
            result->edges[index].end = edge.end;
            result->edges[index].startTangentX = edge.startTangent.x;
            result->edges[index].startTangentY = edge.startTangent.y;
            result->edges[index].endTangentX = edge.endTangent.x;
            result->edges[index].endTangentY = edge.endTangent.y;
        }
    }
    return result;
}

motion::VectorNetwork FromMSVectorNetwork(const MSVectorNetwork *network) {
    motion::VectorNetwork result;
    if (network == nullptr) {
        return result;
    }
    if (network->vertices != nullptr && network->vertexCount > 0) {
        result.vertices.reserve(network->vertexCount);
        for (size_t index = 0; index < network->vertexCount; ++index) {
            const MSVectorNetworkVertex &vertex = network->vertices[index];
            result.vertices.push_back(
                {vertex.id, motion::Vec2{vertex.x, vertex.y}});
        }
    }
    if (network->edges != nullptr && network->edgeCount > 0) {
        result.edges.reserve(network->edgeCount);
        for (size_t index = 0; index < network->edgeCount; ++index) {
            const MSVectorNetworkEdge &edge = network->edges[index];
            motion::VectorNetwork::Edge out;
            out.id = edge.id;
            out.start = edge.start;
            out.end = edge.end;
            out.startTangent = {edge.startTangentX, edge.startTangentY};
            out.endTangent = {edge.endTangentX, edge.endTangentY};
            result.edges.push_back(out);
        }
    }
    return result;
}

void Execute(MSDocument *handle, std::unique_ptr<motion::Command> command) {
    if (handle == nullptr) {
        return;
    }
    handle->undoManager->execute(*handle->document, std::move(command));
}

PropertyPath MakePath(uint64_t entityId, const char *path) {
    return PropertyPath{EntityId{entityId}, path != nullptr ? path : ""};
}

motion::BlendMode MakeBlendMode(MS_BLEND blendMode) {
    if (blendMode < MS_BLEND_NORMAL || blendMode > MS_BLEND_ADD) {
        return motion::BlendMode::Normal;
    }
    return static_cast<motion::BlendMode>(blendMode);
}

motion::StrokePosition MakeStrokePosition(MS_STROKE_POSITION position) {
    if (position < MS_STROKE_POSITION_CENTER || position > MS_STROKE_POSITION_OUTSIDE) {
        return motion::StrokePosition::Center;
    }
    return static_cast<motion::StrokePosition>(position);
}

motion::MaskMode MakeMaskMode(MS_MASK mode) {
    if (mode < MS_MASK_ADD || mode > MS_MASK_INTERSECT) {
        return motion::MaskMode::Add;
    }
    return static_cast<motion::MaskMode>(mode);
}

motion::TrackMatteType MakeTrackMatteType(MS_TRACK_MATTE type) {
    if (type < MS_TRACK_MATTE_NONE || type > MS_TRACK_MATTE_LUMA_INVERTED) {
        return motion::TrackMatteType::None;
    }
    return static_cast<motion::TrackMatteType>(type);
}

motion::Mask MakeMaskFromLayer(const Layer &layer, int64_t frame) {
    motion::Mask mask;
    mask.path.setStaticValue(BridgeNetworkFromPath(motion::BakeMaskPathFromLayer(layer, frame)));
    return mask;
}

Easing MakeEasing(int easingType, float inX, float inY, float outX, float outY) {
    switch (easingType) {
        case MS_EASING_HOLD: {
            return Easing::Hold();
        }
        case MS_EASING_EASE: {
            return Easing::Ease();
        }
        case MS_EASING_EASE_IN: {
            return Easing::EaseIn();
        }
        case MS_EASING_EASE_OUT: {
            return Easing::EaseOut();
        }
        case MS_EASING_EASE_IN_OUT: {
            return Easing::EaseInOut();
        }
        case MS_EASING_CUBIC_BEZIER: {
            return Easing::Bezier(inX, inY, outX, outY);
        }
        default: {
            return Easing::Linear();
        }
    }
}

namespace {

const Color SHAPE_PALETTE[6] = {
    {0.29f, 0.56f, 0.89f, 1.0f},
    {0.91f, 0.52f, 0.29f, 1.0f},
    {0.40f, 0.76f, 0.45f, 1.0f},
    {0.69f, 0.42f, 0.87f, 1.0f},
    {0.96f, 0.71f, 0.25f, 1.0f},
    {0.90f, 0.38f, 0.45f, 1.0f},
};

}  // namespace

uint64_t AddShapeLayer(MSDocument *handle, uint64_t compositionId, bool ellipse) {
    Composition *composition = FindComposition(handle, compositionId);
    if (composition == nullptr) {
        return 0;
    }
    auto layer = std::make_unique<Layer>(LayerType::Shape);
    layer->name = (ellipse ? "Ellipse " : "Rectangle ") + std::to_string(composition->layers.size() + 1);
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    layer->transform.position.setStaticValue(Vec2{composition->width * 0.5f, composition->height * 0.5f});

    auto *content = static_cast<ShapeContent *>(layer->content.get());
    if (ellipse) {
        auto shape = std::make_unique<ShapeEllipse>();
        shape->size.setStaticValue(Vec2{200.0f, 200.0f});
        content->geometry = std::move(shape);
    } else {
        auto shape = std::make_unique<ShapeRect>();
        shape->size.setStaticValue(Vec2{200.0f, 200.0f});
        content->geometry = std::move(shape);
    }
    auto fill = std::make_unique<FillStyle>();
    fill->color.setStaticValue(SHAPE_PALETTE[composition->layers.size() % 6]);
    layer->styles.push_back(std::move(fill));

    const uint64_t layerId = layer->id.value;
    Execute(handle, std::make_unique<motion::AddLayerCommand>(composition->id, std::move(layer)));
    return layerId;
}

uint64_t AddPathLayer(MSDocument *handle, uint64_t compositionId) {
    Composition *composition = FindComposition(handle, compositionId);
    if (composition == nullptr) {
        return 0;
    }
    auto layer = std::make_unique<Layer>(LayerType::Shape);
    layer->name = "Path " + std::to_string(composition->layers.size() + 1);
    layer->inPoint = 0;
    layer->outPoint = composition->duration;
    layer->transform.position.setStaticValue(Vec2{composition->width * 0.5f, composition->height * 0.5f});

    auto *content = static_cast<ShapeContent *>(layer->content.get());
    content->geometry = std::make_unique<ShapePath>();

    // Pen-created paths default to a stroke like Figma; fill is added manually.
    auto stroke = std::make_unique<StrokeStyle>();
    stroke->color.setStaticValue(SHAPE_PALETTE[composition->layers.size() % 6]);
    stroke->width.setStaticValue(1.0f);
    layer->styles.push_back(std::move(stroke));

    const uint64_t layerId = layer->id.value;
    Execute(handle, std::make_unique<motion::AddLayerCommand>(composition->id, std::move(layer)));
    return layerId;
}

void ResolvePointTextContainerSizes(motion::SceneState &state) {
    for (motion::EvaluatedLayer &layer : state.layers) {
        if (!layer.textItem.has_value()) {
            continue;
        }
        motion::EvaluatedTextItem &item = *layer.textItem;
        if (item.textPath.has_value() && !item.textPath->path.contours.empty()) {
            const motion::EvaluatedTextPath &path = *item.textPath;
            const motion::TextPathBounds bounds = MeasureTextPathBounds(
                item.text, item.fontSize, item.align, item.fontFamily, item.fontStyle, path.path,
                path.reversed, path.perpendicular, path.forceAlignment, path.firstMargin,
                path.lastMargin);
            item.useExactLocalBounds = true;
            item.localBoundsMin = bounds.min;
            item.localBoundsMax = bounds.max;
            item.containerSize = {std::max(1.0f, bounds.max.x - bounds.min.x),
                                  std::max(1.0f, bounds.max.y - bounds.min.y)};
            continue;
        }
        if (item.boxTextMode) {
            continue;
        }
        item.useExactLocalBounds = false;
        item.containerSize = MeasurePointTextSize(item.text, item.fontSize, item.align, item.fontFamily,
                                                  item.fontStyle);
        item.localBoundsMin = {0.0f, 0.0f};
        item.localBoundsMax = item.containerSize;
    }
}

}  // namespace bridge
