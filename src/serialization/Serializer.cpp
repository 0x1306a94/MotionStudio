#include "MotionStudio/serialization/Serializer.h"

#include <cstdio>

#include <nlohmann/json.hpp>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/NullContent.h"
#include "MotionStudio/model/PrecompContent.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapeFill.h"
#include "MotionStudio/model/ShapeGroup.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/ShapeStroke.h"
#include "MotionStudio/model/ShapeTrimPath.h"
#include "MotionStudio/model/TextContent.h"
#include "MotionStudio/serialization/Dto.h"
#include "MotionStudio/serialization/SchemaMigrator.h"

namespace motion {

using json = nlohmann::json;

namespace {

// ---- EntityId ↔ 16 字符 hex ----

std::string idToString(EntityId id) {
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx",
                  static_cast<unsigned long long>(id.value));
    return buffer;
}

EntityId idFromString(const std::string& text) {
    return EntityId{std::stoull(text, nullptr, 16)};
}

// ---- 叶子值（结构错误由 nlohmann 抛出，在 deserialize 边界统一转换）----

json vec2ToJson(Vec2 value) {
    return json::array({value.x, value.y});
}

Vec2 vec2FromJson(const json& node) {
    return {node.at(0).get<float>(), node.at(1).get<float>()};
}

json colorToJson(Color value) {
    return json::array({value.r, value.g, value.b, value.a});
}

Color colorFromJson(const json& node) {
    return {node.at(0).get<float>(), node.at(1).get<float>(), node.at(2).get<float>(),
            node.at(3).get<float>()};
}

json bezierPathToJson(const BezierPath& path) {
    json vertices = json::array();
    for (const BezierPath::Vertex& vertex : path.vertices) {
        vertices.push_back({{"point", vec2ToJson(vertex.point)},
                            {"inTangent", vec2ToJson(vertex.inTangent)},
                            {"outTangent", vec2ToJson(vertex.outTangent)}});
    }
    return {{"closed", path.closed}, {"vertices", vertices}};
}

BezierPath bezierPathFromJson(const json& node) {
    BezierPath path;
    path.closed = node.at("closed").get<bool>();
    for (const json& vertexNode : node.at("vertices")) {
        path.vertices.push_back({vec2FromJson(vertexNode.at("point")),
                                 vec2FromJson(vertexNode.at("inTangent")),
                                 vec2FromJson(vertexNode.at("outTangent"))});
    }
    return path;
}

// 属性值 → JSON 节点（按值类型分发）。
json valueNode(float value) { return value; }
json valueNode(const Vec2& value) { return vec2ToJson(value); }
json valueNode(const Color& value) { return colorToJson(value); }
json valueNode(const BezierPath& value) { return bezierPathToJson(value); }
json valueNode(const std::string& value) { return value; }

template <typename T>
T valueFromNode(const json& node);

template <>
float valueFromNode<float>(const json& node) {
    return node.get<float>();
}
template <>
Vec2 valueFromNode<Vec2>(const json& node) {
    return vec2FromJson(node);
}
template <>
Color valueFromNode<Color>(const json& node) {
    return colorFromJson(node);
}
template <>
BezierPath valueFromNode<BezierPath>(const json& node) {
    return bezierPathFromJson(node);
}
template <>
std::string valueFromNode<std::string>(const json& node) {
    return node.get<std::string>();
}

// ---- Easing ----

json easingToJson(const Easing& easing) {
    json node{{"type", dto::toString(easing.type)}};
    if (easing.type == Easing::Type::Bezier) {
        node["inX"] = easing.inX;
        node["inY"] = easing.inY;
        node["outX"] = easing.outX;
        node["outY"] = easing.outY;
    }
    return node;
}

Expected<Easing> easingFromJson(const json& node) {
    Expected<Easing::Type> type =
        dto::easingTypeFromString(node.at("type").get<std::string>());
    if (!type) {
        return Error(type.errorMessage());
    }
    if (*type == Easing::Type::Bezier) {
        return Easing::Bezier(node.at("inX").get<float>(), node.at("inY").get<float>(),
                              node.at("outX").get<float>(), node.at("outY").get<float>());
    }
    return *type == Easing::Type::Hold ? Easing::Hold() : Easing::Linear();
}

// ---- Animatable<T>：静态 {"static": ...} / 关键帧 {"keyframes": [...]} ----

template <typename T>
json animatableToJson(const Animatable<T>& animatable) {
    if (!animatable.isAnimated()) {
        return json{{"static", valueNode(animatable.staticValue())}};
    }
    json keyframes = json::array();
    for (const Keyframe<T>& keyframe : animatable.keyframes()) {
        json node{{"time", keyframe.time},
                  {"value", valueNode(keyframe.value)},
                  {"easing", easingToJson(keyframe.easing)}};
        if (keyframe.spatialInTangent) {
            node["spatialInTangent"] = vec2ToJson(*keyframe.spatialInTangent);
        }
        if (keyframe.spatialOutTangent) {
            node["spatialOutTangent"] = vec2ToJson(*keyframe.spatialOutTangent);
        }
        keyframes.push_back(std::move(node));
    }
    return json{{"keyframes", std::move(keyframes)}};
}

template <typename T>
Expected<Keyframe<T>> keyframeFromJson(const json& node) {
    Keyframe<T> keyframe;
    keyframe.time = node.at("time").get<FrameTime>();
    keyframe.value = valueFromNode<T>(node.at("value"));
    Expected<Easing> easing = easingFromJson(node.at("easing"));
    if (!easing) {
        return Error(easing.errorMessage());
    }
    keyframe.easing = *easing;
    if (node.contains("spatialInTangent")) {
        keyframe.spatialInTangent = vec2FromJson(node.at("spatialInTangent"));
    }
    if (node.contains("spatialOutTangent")) {
        keyframe.spatialOutTangent = vec2FromJson(node.at("spatialOutTangent"));
    }
    return keyframe;
}

template <typename T>
Expected<void> animatableFromJson(const json& node, Animatable<T>& animatable) {
    if (node.contains("static")) {
        animatable.setStaticValue(valueFromNode<T>(node.at("static")));
    }
    if (node.contains("keyframes")) {
        for (const json& keyframeNode : node.at("keyframes")) {
            Expected<Keyframe<T>> keyframe = keyframeFromJson<T>(keyframeNode);
            if (!keyframe) {
                return Error(keyframe.errorMessage());
            }
            animatable.addKeyframe(std::move(*keyframe));
        }
    }
    return {};
}

// ---- Transform / Mask ----

json transformToJson(const Transform& transform) {
    return {{"anchorPoint", animatableToJson(transform.anchorPoint)},
            {"position", animatableToJson(transform.position)},
            {"scale", animatableToJson(transform.scale)},
            {"rotation", animatableToJson(transform.rotation)},
            {"opacity", animatableToJson(transform.opacity)}};
}

Expected<void> transformFromJson(const json& node, Transform& transform) {
    Expected<void> result = animatableFromJson(node.at("anchorPoint"), transform.anchorPoint);
    if (!result) {
        return result;
    }
    result = animatableFromJson(node.at("position"), transform.position);
    if (!result) {
        return result;
    }
    result = animatableFromJson(node.at("scale"), transform.scale);
    if (!result) {
        return result;
    }
    result = animatableFromJson(node.at("rotation"), transform.rotation);
    if (!result) {
        return result;
    }
    return animatableFromJson(node.at("opacity"), transform.opacity);
}

json maskToJson(const Mask& mask) {
    return {{"path", bezierPathToJson(mask.path)},
            {"mode", dto::toString(mask.mode)},
            {"opacity", animatableToJson(mask.opacity)},
            {"inverted", mask.inverted}};
}

Expected<Mask> maskFromJson(const json& node) {
    Mask mask;
    mask.path = bezierPathFromJson(node.at("path"));
    Expected<MaskMode> mode = dto::maskModeFromString(node.at("mode").get<std::string>());
    if (!mode) {
        return Error(mode.errorMessage());
    }
    mask.mode = *mode;
    Expected<void> result = animatableFromJson(node.at("opacity"), mask.opacity);
    if (!result) {
        return Error(result.errorMessage());
    }
    mask.inverted = node.at("inverted").get<bool>();
    return mask;
}

// ---- Shape 元素（判别字段 "type"）----

Expected<std::unique_ptr<ShapeElement>> shapeFromJson(const json& node);

json shapesToJson(const std::vector<std::unique_ptr<ShapeElement>>& elements) {
    json nodes = json::array();
    for (const auto& element : elements) {
        json node{{"id", idToString(element->id)},
                  {"type", dto::toString(element->type())}};
        switch (element->type()) {
            case ShapeType::Path: {
                const auto& shape = static_cast<const ShapePath&>(*element);
                node["path"] = animatableToJson(shape.path);
                break;
            }
            case ShapeType::Fill: {
                const auto& shape = static_cast<const ShapeFill&>(*element);
                node["color"] = animatableToJson(shape.color);
                node["opacity"] = animatableToJson(shape.opacity);
                node["fillRule"] = dto::toString(shape.fillRule);
                break;
            }
            case ShapeType::Stroke: {
                const auto& shape = static_cast<const ShapeStroke&>(*element);
                node["color"] = animatableToJson(shape.color);
                node["width"] = animatableToJson(shape.width);
                node["opacity"] = animatableToJson(shape.opacity);
                node["cap"] = dto::toString(shape.cap);
                node["join"] = dto::toString(shape.join);
                node["miterLimit"] = shape.miterLimit;
                break;
            }
            case ShapeType::Group: {
                const auto& shape = static_cast<const ShapeGroup&>(*element);
                node["transform"] = transformToJson(shape.transform);
                node["elements"] = shapesToJson(shape.elements);
                break;
            }
            case ShapeType::Rect: {
                const auto& shape = static_cast<const ShapeRect&>(*element);
                node["position"] = animatableToJson(shape.position);
                node["size"] = animatableToJson(shape.size);
                node["cornerRadius"] = animatableToJson(shape.cornerRadius);
                break;
            }
            case ShapeType::Ellipse: {
                const auto& shape = static_cast<const ShapeEllipse&>(*element);
                node["position"] = animatableToJson(shape.position);
                node["size"] = animatableToJson(shape.size);
                break;
            }
            case ShapeType::TrimPath: {
                const auto& shape = static_cast<const ShapeTrimPath&>(*element);
                node["start"] = animatableToJson(shape.start);
                node["end"] = animatableToJson(shape.end);
                node["offset"] = animatableToJson(shape.offset);
                break;
            }
        }
        nodes.push_back(std::move(node));
    }
    return nodes;
}

Expected<std::unique_ptr<ShapeElement>> shapeFromJson(const json& node) {
    Expected<ShapeType> type = dto::shapeTypeFromString(node.at("type").get<std::string>());
    if (!type) {
        return Error(type.errorMessage());
    }
    std::unique_ptr<ShapeElement> element;
    switch (*type) {
        case ShapeType::Path: {
            auto shape = std::make_unique<ShapePath>();
            Expected<void> result = animatableFromJson(node.at("path"), shape->path);
            if (!result) {
                return Error(result.errorMessage());
            }
            element = std::move(shape);
            break;
        }
        case ShapeType::Fill: {
            auto shape = std::make_unique<ShapeFill>();
            Expected<void> result = animatableFromJson(node.at("color"), shape->color);
            if (!result) {
                return Error(result.errorMessage());
            }
            result = animatableFromJson(node.at("opacity"), shape->opacity);
            if (!result) {
                return Error(result.errorMessage());
            }
            Expected<FillRule> fillRule =
                dto::fillRuleFromString(node.at("fillRule").get<std::string>());
            if (!fillRule) {
                return Error(fillRule.errorMessage());
            }
            shape->fillRule = *fillRule;
            element = std::move(shape);
            break;
        }
        case ShapeType::Stroke: {
            auto shape = std::make_unique<ShapeStroke>();
            Expected<void> result = animatableFromJson(node.at("color"), shape->color);
            if (!result) {
                return Error(result.errorMessage());
            }
            result = animatableFromJson(node.at("width"), shape->width);
            if (!result) {
                return Error(result.errorMessage());
            }
            result = animatableFromJson(node.at("opacity"), shape->opacity);
            if (!result) {
                return Error(result.errorMessage());
            }
            Expected<LineCap> cap = dto::lineCapFromString(node.at("cap").get<std::string>());
            if (!cap) {
                return Error(cap.errorMessage());
            }
            Expected<LineJoin> join =
                dto::lineJoinFromString(node.at("join").get<std::string>());
            if (!join) {
                return Error(join.errorMessage());
            }
            shape->cap = *cap;
            shape->join = *join;
            shape->miterLimit = node.at("miterLimit").get<float>();
            element = std::move(shape);
            break;
        }
        case ShapeType::Group: {
            auto shape = std::make_unique<ShapeGroup>();
            Expected<void> result =
                transformFromJson(node.at("transform"), shape->transform);
            if (!result) {
                return Error(result.errorMessage());
            }
            for (const json& childNode : node.at("elements")) {
                Expected<std::unique_ptr<ShapeElement>> child = shapeFromJson(childNode);
                if (!child) {
                    return Error(child.errorMessage());
                }
                shape->elements.push_back(std::move(*child));
            }
            element = std::move(shape);
            break;
        }
        case ShapeType::Rect: {
            auto shape = std::make_unique<ShapeRect>();
            Expected<void> result =
                animatableFromJson(node.at("position"), shape->position);
            if (!result) {
                return Error(result.errorMessage());
            }
            result = animatableFromJson(node.at("size"), shape->size);
            if (!result) {
                return Error(result.errorMessage());
            }
            result = animatableFromJson(node.at("cornerRadius"), shape->cornerRadius);
            if (!result) {
                return Error(result.errorMessage());
            }
            element = std::move(shape);
            break;
        }
        case ShapeType::Ellipse: {
            auto shape = std::make_unique<ShapeEllipse>();
            Expected<void> result =
                animatableFromJson(node.at("position"), shape->position);
            if (!result) {
                return Error(result.errorMessage());
            }
            result = animatableFromJson(node.at("size"), shape->size);
            if (!result) {
                return Error(result.errorMessage());
            }
            element = std::move(shape);
            break;
        }
        case ShapeType::TrimPath: {
            auto shape = std::make_unique<ShapeTrimPath>();
            Expected<void> result = animatableFromJson(node.at("start"), shape->start);
            if (!result) {
                return Error(result.errorMessage());
            }
            result = animatableFromJson(node.at("end"), shape->end);
            if (!result) {
                return Error(result.errorMessage());
            }
            result = animatableFromJson(node.at("offset"), shape->offset);
            if (!result) {
                return Error(result.errorMessage());
            }
            element = std::move(shape);
            break;
        }
    }
    element->id = idFromString(node.at("id").get<std::string>());
    return element;
}

// ---- LayerContent（判别字段 "type"）----

json contentToJson(const LayerContent& content) {
    json node{{"type", dto::toString(content.type())}};
    switch (content.type()) {
        case LayerType::Shape: {
            const auto& shape = static_cast<const ShapeContent&>(content);
            node["elements"] = shapesToJson(shape.elements);
            break;
        }
        case LayerType::Image: {
            const auto& image = static_cast<const ImageContent&>(content);
            node["assetId"] = idToString(image.assetId);
            break;
        }
        case LayerType::Text: {
            const auto& text = static_cast<const TextContent&>(content);
            node["text"] = animatableToJson(text.text);
            node["fontFamily"] = text.fontFamily;
            node["fontSize"] = animatableToJson(text.fontSize);
            break;
        }
        case LayerType::Null:
            break;
        case LayerType::Precomp: {
            const auto& precomp = static_cast<const PrecompContent&>(content);
            node["compositionId"] = idToString(precomp.compositionId);
            break;
        }
    }
    return node;
}

Expected<std::unique_ptr<LayerContent>> contentFromJson(const json& node) {
    Expected<LayerType> type = dto::layerTypeFromString(node.at("type").get<std::string>());
    if (!type) {
        return Error(type.errorMessage());
    }
    switch (*type) {
        case LayerType::Shape: {
            auto content = std::make_unique<ShapeContent>();
            for (const json& elementNode : node.at("elements")) {
                Expected<std::unique_ptr<ShapeElement>> element = shapeFromJson(elementNode);
                if (!element) {
                    return Error(element.errorMessage());
                }
                content->elements.push_back(std::move(*element));
            }
            return std::unique_ptr<LayerContent>(std::move(content));
        }
        case LayerType::Image: {
            auto content = std::make_unique<ImageContent>();
            content->assetId = idFromString(node.at("assetId").get<std::string>());
            return std::unique_ptr<LayerContent>(std::move(content));
        }
        case LayerType::Text: {
            auto content = std::make_unique<TextContent>();
            Expected<void> result = animatableFromJson(node.at("text"), content->text);
            if (!result) {
                return Error(result.errorMessage());
            }
            content->fontFamily = node.at("fontFamily").get<std::string>();
            result = animatableFromJson(node.at("fontSize"), content->fontSize);
            if (!result) {
                return Error(result.errorMessage());
            }
            return std::unique_ptr<LayerContent>(std::move(content));
        }
        case LayerType::Null:
            return std::unique_ptr<LayerContent>(std::make_unique<NullContent>());
        case LayerType::Precomp: {
            auto content = std::make_unique<PrecompContent>();
            content->compositionId =
                idFromString(node.at("compositionId").get<std::string>());
            return std::unique_ptr<LayerContent>(std::move(content));
        }
    }
    return Error("未知 layer content 类型");
}

// ---- Layer / Composition / Asset / Document ----

json layerToJson(const Layer& layer) {
    json node{{"id", idToString(layer.id)},
              {"name", layer.name},
              {"type", dto::toString(layer.type())},
              {"inPoint", layer.inPoint},
              {"outPoint", layer.outPoint},
              {"startTime", layer.startTime},
              {"timeStretch", layer.timeStretch},
              {"visible", layer.visible},
              {"locked", layer.locked},
              {"blendMode", dto::toString(layer.blendMode)},
              {"transform", transformToJson(layer.transform)},
              {"content", contentToJson(*layer.content)}};
    if (layer.parentId.isValid()) {
        node["parentId"] = idToString(layer.parentId);
    } else {
        node["parentId"] = nullptr;
    }
    json masks = json::array();
    for (const Mask& mask : layer.masks) {
        masks.push_back(maskToJson(mask));
    }
    node["masks"] = std::move(masks);
    return node;
}

Expected<std::unique_ptr<Layer>> layerFromJson(const json& node) {
    Expected<LayerType> type = dto::layerTypeFromString(node.at("type").get<std::string>());
    if (!type) {
        return Error(type.errorMessage());
    }
    auto layer = std::make_unique<Layer>(*type);
    layer->id = idFromString(node.at("id").get<std::string>());
    layer->name = node.at("name").get<std::string>();
    layer->inPoint = node.at("inPoint").get<FrameTime>();
    layer->outPoint = node.at("outPoint").get<FrameTime>();
    layer->startTime = node.at("startTime").get<FrameTime>();
    layer->timeStretch = node.at("timeStretch").get<double>();
    layer->visible = node.at("visible").get<bool>();
    layer->locked = node.at("locked").get<bool>();

    Expected<BlendMode> blendMode =
        dto::blendModeFromString(node.at("blendMode").get<std::string>());
    if (!blendMode) {
        return Error(blendMode.errorMessage());
    }
    layer->blendMode = *blendMode;

    Expected<void> result = transformFromJson(node.at("transform"), layer->transform);
    if (!result) {
        return Error(result.errorMessage());
    }
    Expected<std::unique_ptr<LayerContent>> content = contentFromJson(node.at("content"));
    if (!content) {
        return Error(content.errorMessage());
    }
    layer->content = std::move(*content);

    if (!node.at("parentId").is_null()) {
        layer->parentId = idFromString(node.at("parentId").get<std::string>());
    }
    for (const json& maskNode : node.at("masks")) {
        Expected<Mask> mask = maskFromJson(maskNode);
        if (!mask) {
            return Error(mask.errorMessage());
        }
        layer->masks.push_back(std::move(*mask));
    }
    return layer;
}

json compositionToJson(const Composition& composition) {
    json layers = json::array();
    for (const auto& layer : composition.layers) {
        layers.push_back(layerToJson(*layer));
    }
    return {{"id", idToString(composition.id)},
            {"name", composition.name},
            {"duration", composition.duration},
            {"frameRate",
             {{"num", composition.frameRate.num}, {"den", composition.frameRate.den}}},
            {"width", composition.width},
            {"height", composition.height},
            {"backgroundColor", colorToJson(composition.backgroundColor)},
            {"layers", std::move(layers)}};
}

Expected<std::unique_ptr<Composition>> compositionFromJson(const json& node) {
    auto composition = std::make_unique<Composition>();
    composition->id = idFromString(node.at("id").get<std::string>());
    composition->name = node.at("name").get<std::string>();
    composition->duration = node.at("duration").get<FrameTime>();
    const json& frameRate = node.at("frameRate");
    composition->frameRate = {frameRate.at("num").get<uint32_t>(),
                              frameRate.at("den").get<uint32_t>()};
    composition->width = node.at("width").get<int>();
    composition->height = node.at("height").get<int>();
    composition->backgroundColor = colorFromJson(node.at("backgroundColor"));
    for (const json& layerNode : node.at("layers")) {
        Expected<std::unique_ptr<Layer>> layer = layerFromJson(layerNode);
        if (!layer) {
            return Error(layer.errorMessage());
        }
        composition->layers.push_back(std::move(*layer));
    }
    return composition;
}

json assetToJson(const Asset& asset) {
    return {{"id", idToString(asset.id)},
            {"type", dto::toString(asset.type)},
            {"name", asset.name},
            {"path", asset.path}};
}

Expected<Asset> assetFromJson(const json& node) {
    Asset asset;
    asset.id = idFromString(node.at("id").get<std::string>());
    Expected<AssetType> type = dto::assetTypeFromString(node.at("type").get<std::string>());
    if (!type) {
        return Error(type.errorMessage());
    }
    asset.type = *type;
    asset.name = node.at("name").get<std::string>();
    asset.path = node.at("path").get<std::string>();
    return asset;
}

json documentToJson(const Document& document) {
    json compositions = json::array();
    for (const auto& composition : document.compositions) {
        compositions.push_back(compositionToJson(*composition));
    }
    json assets = json::array();
    for (const Asset& asset : document.assets) {
        assets.push_back(assetToJson(asset));
    }
    return {{"schemaVersion", dto::kSchemaVersion},
            {"id", idToString(document.id)},
            {"name", document.name},
            {"assets", std::move(assets)},
            {"compositions", std::move(compositions)}};
}

}  // namespace

std::string Serializer::serialize(const Document& document) {
    return documentToJson(document).dump(2);
}

Expected<std::unique_ptr<Document>> Serializer::deserialize(const std::string& jsonText) {
    try {
        Expected<std::string> migrated = SchemaMigrator::migrate(jsonText);
        if (!migrated) {
            return Error(migrated.errorMessage());
        }
        const json data = json::parse(*migrated);

        auto document = std::make_unique<Document>();
        document->id = idFromString(data.at("id").get<std::string>());
        document->name = data.at("name").get<std::string>();
        for (const json& assetNode : data.at("assets")) {
            Expected<Asset> asset = assetFromJson(assetNode);
            if (!asset) {
                return Error(asset.errorMessage());
            }
            document->assets.push_back(std::move(*asset));
        }
        for (const json& compositionNode : data.at("compositions")) {
            Expected<std::unique_ptr<Composition>> composition =
                compositionFromJson(compositionNode);
            if (!composition) {
                return Error(composition.errorMessage());
            }
            document->compositions.push_back(std::move(*composition));
        }
        document->refreshEntityIndex();
        return document;
    } catch (const std::exception& error) {
        // nlohmann 内部抛出的结构性错误在此边界统一转换为 Error。
        return Error(std::string("文档 JSON 解析失败: ") + error.what());
    }
}

uint64_t documentFingerprint(const Document& document) {
    const std::string compact = documentToJson(document).dump();
    uint64_t hash = 14695981039346656037ull;  // FNV-1a 64
    for (const unsigned char byte : compact) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

}  // namespace motion
