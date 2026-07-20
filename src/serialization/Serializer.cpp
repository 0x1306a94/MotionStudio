#include "MotionStudio/serialization/Serializer.h"

#include <cstdio>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/model/Document.h"
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

// ---- 叶子值 ----

json vec2ToJson(Vec2 value) { return json::array({value.x, value.y}); }

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

Easing easingFromJson(const json& node) {
    const Easing::Type type = dto::easingTypeFromString(node.at("type").get<std::string>());
    if (type == Easing::Type::Bezier) {
        return Easing::Bezier(node.at("inX").get<float>(), node.at("inY").get<float>(),
                              node.at("outX").get<float>(), node.at("outY").get<float>());
    }
    return type == Easing::Type::Hold ? Easing::Hold() : Easing::Linear();
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
void animatableFromJson(const json& node, Animatable<T>& animatable) {
    if (node.contains("static")) {
        animatable.setStaticValue(valueFromNode<T>(node.at("static")));
    }
    if (node.contains("keyframes")) {
        for (const json& keyframeNode : node.at("keyframes")) {
            Keyframe<T> keyframe;
            keyframe.time = keyframeNode.at("time").get<FrameTime>();
            keyframe.value = valueFromNode<T>(keyframeNode.at("value"));
            keyframe.easing = easingFromJson(keyframeNode.at("easing"));
            if (keyframeNode.contains("spatialInTangent")) {
                keyframe.spatialInTangent =
                    vec2FromJson(keyframeNode.at("spatialInTangent"));
            }
            if (keyframeNode.contains("spatialOutTangent")) {
                keyframe.spatialOutTangent =
                    vec2FromJson(keyframeNode.at("spatialOutTangent"));
            }
            animatable.addKeyframe(std::move(keyframe));
        }
    }
}

// ---- Transform / Mask ----

json transformToJson(const Transform& transform) {
    return {{"anchorPoint", animatableToJson(transform.anchorPoint)},
            {"position", animatableToJson(transform.position)},
            {"scale", animatableToJson(transform.scale)},
            {"rotation", animatableToJson(transform.rotation)},
            {"opacity", animatableToJson(transform.opacity)}};
}

void transformFromJson(const json& node, Transform& transform) {
    animatableFromJson(node.at("anchorPoint"), transform.anchorPoint);
    animatableFromJson(node.at("position"), transform.position);
    animatableFromJson(node.at("scale"), transform.scale);
    animatableFromJson(node.at("rotation"), transform.rotation);
    animatableFromJson(node.at("opacity"), transform.opacity);
}

json maskToJson(const Mask& mask) {
    return {{"path", bezierPathToJson(mask.path)},
            {"mode", dto::toString(mask.mode)},
            {"opacity", animatableToJson(mask.opacity)},
            {"inverted", mask.inverted}};
}

Mask maskFromJson(const json& node) {
    Mask mask;
    mask.path = bezierPathFromJson(node.at("path"));
    mask.mode = dto::maskModeFromString(node.at("mode").get<std::string>());
    animatableFromJson(node.at("opacity"), mask.opacity);
    mask.inverted = node.at("inverted").get<bool>();
    return mask;
}

// ---- Shape 元素（判别字段 "type"）----

json shapesToJson(const std::vector<std::unique_ptr<ShapeElement>>& elements);

json shapeToJson(const ShapeElement& element) {
    json node{{"id", idToString(element.id)}, {"type", dto::toString(element.type())}};
    switch (element.type()) {
        case ShapeType::Path: {
            const auto& shape = static_cast<const ShapePath&>(element);
            node["path"] = animatableToJson(shape.path);
            break;
        }
        case ShapeType::Fill: {
            const auto& shape = static_cast<const ShapeFill&>(element);
            node["color"] = animatableToJson(shape.color);
            node["opacity"] = animatableToJson(shape.opacity);
            node["fillRule"] = dto::toString(shape.fillRule);
            break;
        }
        case ShapeType::Stroke: {
            const auto& shape = static_cast<const ShapeStroke&>(element);
            node["color"] = animatableToJson(shape.color);
            node["width"] = animatableToJson(shape.width);
            node["opacity"] = animatableToJson(shape.opacity);
            node["cap"] = dto::toString(shape.cap);
            node["join"] = dto::toString(shape.join);
            node["miterLimit"] = shape.miterLimit;
            break;
        }
        case ShapeType::Group: {
            const auto& shape = static_cast<const ShapeGroup&>(element);
            node["transform"] = transformToJson(shape.transform);
            node["elements"] = shapesToJson(shape.elements);
            break;
        }
        case ShapeType::Rect: {
            const auto& shape = static_cast<const ShapeRect&>(element);
            node["position"] = animatableToJson(shape.position);
            node["size"] = animatableToJson(shape.size);
            node["cornerRadius"] = animatableToJson(shape.cornerRadius);
            break;
        }
        case ShapeType::Ellipse: {
            const auto& shape = static_cast<const ShapeEllipse&>(element);
            node["position"] = animatableToJson(shape.position);
            node["size"] = animatableToJson(shape.size);
            break;
        }
        case ShapeType::TrimPath: {
            const auto& shape = static_cast<const ShapeTrimPath&>(element);
            node["start"] = animatableToJson(shape.start);
            node["end"] = animatableToJson(shape.end);
            node["offset"] = animatableToJson(shape.offset);
            break;
        }
    }
    return node;
}

json shapesToJson(const std::vector<std::unique_ptr<ShapeElement>>& elements) {
    json nodes = json::array();
    for (const auto& element : elements) {
        nodes.push_back(shapeToJson(*element));
    }
    return nodes;
}

std::unique_ptr<ShapeElement> shapeFromJson(const json& node) {
    const ShapeType type = dto::shapeTypeFromString(node.at("type").get<std::string>());
    std::unique_ptr<ShapeElement> element;
    switch (type) {
        case ShapeType::Path: {
            auto shape = std::make_unique<ShapePath>();
            animatableFromJson(node.at("path"), shape->path);
            element = std::move(shape);
            break;
        }
        case ShapeType::Fill: {
            auto shape = std::make_unique<ShapeFill>();
            animatableFromJson(node.at("color"), shape->color);
            animatableFromJson(node.at("opacity"), shape->opacity);
            shape->fillRule = dto::fillRuleFromString(node.at("fillRule").get<std::string>());
            element = std::move(shape);
            break;
        }
        case ShapeType::Stroke: {
            auto shape = std::make_unique<ShapeStroke>();
            animatableFromJson(node.at("color"), shape->color);
            animatableFromJson(node.at("width"), shape->width);
            animatableFromJson(node.at("opacity"), shape->opacity);
            shape->cap = dto::lineCapFromString(node.at("cap").get<std::string>());
            shape->join = dto::lineJoinFromString(node.at("join").get<std::string>());
            shape->miterLimit = node.at("miterLimit").get<float>();
            element = std::move(shape);
            break;
        }
        case ShapeType::Group: {
            auto shape = std::make_unique<ShapeGroup>();
            transformFromJson(node.at("transform"), shape->transform);
            for (const json& childNode : node.at("elements")) {
                shape->elements.push_back(shapeFromJson(childNode));
            }
            element = std::move(shape);
            break;
        }
        case ShapeType::Rect: {
            auto shape = std::make_unique<ShapeRect>();
            animatableFromJson(node.at("position"), shape->position);
            animatableFromJson(node.at("size"), shape->size);
            animatableFromJson(node.at("cornerRadius"), shape->cornerRadius);
            element = std::move(shape);
            break;
        }
        case ShapeType::Ellipse: {
            auto shape = std::make_unique<ShapeEllipse>();
            animatableFromJson(node.at("position"), shape->position);
            animatableFromJson(node.at("size"), shape->size);
            element = std::move(shape);
            break;
        }
        case ShapeType::TrimPath: {
            auto shape = std::make_unique<ShapeTrimPath>();
            animatableFromJson(node.at("start"), shape->start);
            animatableFromJson(node.at("end"), shape->end);
            animatableFromJson(node.at("offset"), shape->offset);
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

std::unique_ptr<LayerContent> contentFromJson(const json& node) {
    const LayerType type = dto::layerTypeFromString(node.at("type").get<std::string>());
    switch (type) {
        case LayerType::Shape: {
            auto content = std::make_unique<ShapeContent>();
            for (const json& elementNode : node.at("elements")) {
                content->elements.push_back(shapeFromJson(elementNode));
            }
            return content;
        }
        case LayerType::Image: {
            auto content = std::make_unique<ImageContent>();
            content->assetId = idFromString(node.at("assetId").get<std::string>());
            return content;
        }
        case LayerType::Text: {
            auto content = std::make_unique<TextContent>();
            animatableFromJson(node.at("text"), content->text);
            content->fontFamily = node.at("fontFamily").get<std::string>();
            animatableFromJson(node.at("fontSize"), content->fontSize);
            return content;
        }
        case LayerType::Null:
            return std::make_unique<NullContent>();
        case LayerType::Precomp: {
            auto content = std::make_unique<PrecompContent>();
            content->compositionId =
                idFromString(node.at("compositionId").get<std::string>());
            return content;
        }
    }
    throw std::invalid_argument("未知 layer content 类型");
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

std::unique_ptr<Layer> layerFromJson(const json& node) {
    const LayerType type = dto::layerTypeFromString(node.at("type").get<std::string>());
    auto layer = std::make_unique<Layer>(type);
    layer->id = idFromString(node.at("id").get<std::string>());
    layer->name = node.at("name").get<std::string>();
    layer->inPoint = node.at("inPoint").get<FrameTime>();
    layer->outPoint = node.at("outPoint").get<FrameTime>();
    layer->startTime = node.at("startTime").get<FrameTime>();
    layer->timeStretch = node.at("timeStretch").get<double>();
    layer->visible = node.at("visible").get<bool>();
    layer->locked = node.at("locked").get<bool>();
    layer->blendMode = dto::blendModeFromString(node.at("blendMode").get<std::string>());
    transformFromJson(node.at("transform"), layer->transform);
    layer->content = contentFromJson(node.at("content"));
    if (!node.at("parentId").is_null()) {
        layer->parentId = idFromString(node.at("parentId").get<std::string>());
    }
    for (const json& maskNode : node.at("masks")) {
        layer->masks.push_back(maskFromJson(maskNode));
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

std::unique_ptr<Composition> compositionFromJson(const json& node) {
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
        composition->layers.push_back(layerFromJson(layerNode));
    }
    return composition;
}

json assetToJson(const Asset& asset) {
    return {{"id", idToString(asset.id)},
            {"type", dto::toString(asset.type)},
            {"name", asset.name},
            {"path", asset.path}};
}

Asset assetFromJson(const json& node) {
    Asset asset;
    asset.id = idFromString(node.at("id").get<std::string>());
    asset.type = dto::assetTypeFromString(node.at("type").get<std::string>());
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

std::unique_ptr<Document> Serializer::deserialize(const std::string& jsonText) {
    try {
        json data = json::parse(SchemaMigrator::migrate(jsonText));

        auto document = std::make_unique<Document>();
        document->id = idFromString(data.at("id").get<std::string>());
        document->name = data.at("name").get<std::string>();
        for (const json& assetNode : data.at("assets")) {
            document->assets.push_back(assetFromJson(assetNode));
        }
        for (const json& compositionNode : data.at("compositions")) {
            document->compositions.push_back(compositionFromJson(compositionNode));
        }
        document->refreshEntityIndex();
        return document;
    } catch (const json::exception& error) {
        throw std::invalid_argument(std::string("文档 JSON 解析失败: ") + error.what());
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
