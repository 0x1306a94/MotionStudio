#include "MotionStudio/serialization/Serializer.h"

#include <charconv>
#include <cstdio>
#include <utility>

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

// ---- JSON non-throwing accessors (project disables try/catch; nlohmann at/get throw) ----

const json *FindChild(const json &node, const char *key) {
    if (!node.is_object()) {
        return nullptr;
    }
    auto it = node.find(key);
    return it == node.end() ? nullptr : &*it;
}

Expected<const json *> Child(const json &node, const char *key) {
    const json *child = FindChild(node, key);
    if (!child) {
        return Error(std::string("missing field: ") + key);
    }
    return child;
}

Expected<float> AsFloat(const json &node) {
    if (!node.is_number()) {
        return Error("field is not a number");
    }
    return node.get<float>();
}

Expected<double> AsDouble(const json &node) {
    if (!node.is_number()) {
        return Error("field is not a number");
    }
    return node.get<double>();
}

Expected<int64_t> AsInt64(const json &node) {
    if (!node.is_number_integer()) {
        return Error("field is not an integer");
    }
    return node.get<int64_t>();
}

Expected<int> AsInt(const json &node) {
    Expected<int64_t> value = AsInt64(node);
    if (!value) {
        return Error(value.errorMessage());
    }
    return int(*value);
}

Expected<uint32_t> AsUint32(const json &node) {
    Expected<int64_t> value = AsInt64(node);
    if (!value || *value < 0 || *value > int64_t(UINT32_MAX)) {
        return Error("field is not a valid unsigned integer");
    }
    return uint32_t(*value);
}

Expected<bool> AsBool(const json &node) {
    if (!node.is_boolean()) {
        return Error("field is not a boolean");
    }
    return node.get<bool>();
}

Expected<std::string> AsString(const json &node) {
    if (!node.is_string()) {
        return Error("field is not a string");
    }
    return node.get<std::string>();
}

// ---- EntityId ↔ 16-char hex ----

std::string IdToString(EntityId id) {
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx",
                  static_cast<unsigned long long>(id.value));
    return buffer;
}

Expected<EntityId> IdFromString(const std::string &text) {
    uint64_t value = 0;
    const char *begin = text.data();
    const char *end = begin + text.size();
    const auto result = std::from_chars(begin, end, value, 16);
    if (result.ec != std::errc() || result.ptr != end || value == 0) {
        return Error("invalid entity id: " + text);
    }
    return EntityId{value};
}

Expected<EntityId> IdField(const json &node, const char *key) {
    Expected<const json *> child = Child(node, key);
    if (!child) {
        return Error(child.errorMessage());
    }
    Expected<std::string> text = AsString(**child);
    if (!text) {
        return Error(text.errorMessage());
    }
    return IdFromString(*text);
}

// ---- Leaf values ----

json Vec2ToJson(Vec2 value) {
    return json::array({value.x, value.y});
}

Expected<Vec2> Vec2FromJson(const json &node) {
    if (!node.is_array() || node.size() != 2) {
        return Error("Vec2 must be a 2-element array");
    }
    Expected<float> x = AsFloat(node[0]);
    if (!x) {
        return Error(x.errorMessage());
    }
    Expected<float> y = AsFloat(node[1]);
    if (!y) {
        return Error(y.errorMessage());
    }
    return Vec2{*x, *y};
}

json ColorToJson(Color value) {
    return json::array({value.r, value.g, value.b, value.a});
}

Expected<Color> ColorFromJson(const json &node) {
    if (!node.is_array() || node.size() != 4) {
        return Error("Color must be a 4-element array");
    }
    Expected<float> r = AsFloat(node[0]);
    Expected<float> g = AsFloat(node[1]);
    Expected<float> b = AsFloat(node[2]);
    Expected<float> a = AsFloat(node[3]);
    if (!r || !g || !b || !a) {
        return Error("Color component is not a number");
    }
    return Color{*r, *g, *b, *a};
}

json BezierPathToJson(const BezierPath &path) {
    json vertices = json::array();
    for (const BezierPath::Vertex &vertex : path.vertices) {
        vertices.push_back({{"point", Vec2ToJson(vertex.point)},
                            {"inTangent", Vec2ToJson(vertex.inTangent)},
                            {"outTangent", Vec2ToJson(vertex.outTangent)}});
    }
    return {{"closed", path.closed}, {"vertices", vertices}};
}

Expected<BezierPath> BezierPathFromJson(const json &node) {
    Expected<const json *> closedNode = Child(node, "closed");
    if (!closedNode) {
        return Error(closedNode.errorMessage());
    }
    Expected<bool> closed = AsBool(**closedNode);
    if (!closed) {
        return Error(closed.errorMessage());
    }
    const json *verticesNode = FindChild(node, "vertices");
    if (!verticesNode || !verticesNode->is_array()) {
        return Error("missing the vertices array");
    }
    BezierPath path;
    path.closed = *closed;
    for (const json &vertexNode : *verticesNode) {
        Expected<const json *> pointNode = Child(vertexNode, "point");
        Expected<const json *> inNode = Child(vertexNode, "inTangent");
        Expected<const json *> outNode = Child(vertexNode, "outTangent");
        if (!pointNode || !inNode || !outNode) {
            return Error("vertex is missing a tangent field");
        }
        Expected<Vec2> point = Vec2FromJson(**pointNode);
        Expected<Vec2> inTangent = Vec2FromJson(**inNode);
        Expected<Vec2> outTangent = Vec2FromJson(**outNode);
        if (!point || !inTangent || !outTangent) {
            return Error("invalid vertex coordinates");
        }
        path.vertices.push_back({*point, *inTangent, *outTangent});
    }
    return path;
}

template <typename T>
Expected<T> FromJson(const json &node);

template <>
Expected<float> FromJson<float>(const json &node) {
    return AsFloat(node);
}
template <>
Expected<double> FromJson<double>(const json &node) {
    return AsDouble(node);
}
template <>
Expected<int> FromJson<int>(const json &node) {
    return AsInt(node);
}
template <>
Expected<int64_t> FromJson<int64_t>(const json &node) {
    return AsInt64(node);
}
template <>
Expected<uint32_t> FromJson<uint32_t>(const json &node) {
    return AsUint32(node);
}
template <>
Expected<bool> FromJson<bool>(const json &node) {
    return AsBool(node);
}
template <>
Expected<std::string> FromJson<std::string>(const json &node) {
    return AsString(node);
}
template <>
Expected<Vec2> FromJson<Vec2>(const json &node) {
    return Vec2FromJson(node);
}
template <>
Expected<Color> FromJson<Color>(const json &node) {
    return ColorFromJson(node);
}
template <>
Expected<BezierPath> FromJson<BezierPath>(const json &node) {
    return BezierPathFromJson(node);
}

template <typename T>
Expected<T> ParseField(const json &node, const char *key) {
    Expected<const json *> child = Child(node, key);
    if (!child) {
        return Error(child.errorMessage());
    }
    return FromJson<T>(**child);
}

// ---- Easing ----

json EasingToJson(const Easing &easing) {
    json node{{"type", dto::ToString(easing.type)}};
    if (easing.type == Easing::Type::Bezier) {
        node["inX"] = easing.inX;
        node["inY"] = easing.inY;
        node["outX"] = easing.outX;
        node["outY"] = easing.outY;
    }
    return node;
}

Expected<Easing> EasingFromJson(const json &node) {
    Expected<std::string> typeText = ParseField<std::string>(node, "type");
    if (!typeText) {
        return Error(typeText.errorMessage());
    }
    Expected<Easing::Type> type = dto::easingTypeFromString(*typeText);
    if (!type) {
        return Error(type.errorMessage());
    }
    if (*type != Easing::Type::Bezier) {
        return *type == Easing::Type::Hold ? Easing::Hold() : Easing::Linear();
    }
    Expected<float> inX = ParseField<float>(node, "inX");
    Expected<float> inY = ParseField<float>(node, "inY");
    Expected<float> outX = ParseField<float>(node, "outX");
    Expected<float> outY = ParseField<float>(node, "outY");
    if (!inX || !inY || !outX || !outY) {
        return Error("Bezier easing is missing control point fields");
    }
    return Easing::Bezier(*inX, *inY, *outX, *outY);
}

// ---- Animatable<T>: static {"static": ...} / keyframes {"keyframes": [...]} ----

json ValueToJson(float value) {
    return value;
}
json ValueToJson(const Vec2 &value) {
    return Vec2ToJson(value);
}
json ValueToJson(const Color &value) {
    return ColorToJson(value);
}
json ValueToJson(const BezierPath &value) {
    return BezierPathToJson(value);
}
json ValueToJson(const std::string &value) {
    return value;
}

template <typename T>
json AnimatableToJson(const Animatable<T> &animatable) {
    if (!animatable.isAnimated()) {
        return json{{"static", ValueToJson(animatable.staticValue())}};
    }
    json keyframes = json::array();
    for (const Keyframe<T> &keyframe : animatable.keyframes()) {
        json node{{"time", keyframe.time},
                  {"value", ValueToJson(keyframe.value)},
                  {"easing", EasingToJson(keyframe.easing)}};
        if (keyframe.spatialInTangent) {
            node["spatialInTangent"] = Vec2ToJson(*keyframe.spatialInTangent);
        }
        if (keyframe.spatialOutTangent) {
            node["spatialOutTangent"] = Vec2ToJson(*keyframe.spatialOutTangent);
        }
        keyframes.push_back(std::move(node));
    }
    return json{{"keyframes", std::move(keyframes)}};
}

template <typename T>
Expected<Keyframe<T>> KeyframeFromJson(const json &node) {
    Expected<int64_t> time = ParseField<int64_t>(node, "time");
    if (!time) {
        return Error(time.errorMessage());
    }
    Expected<T> value = ParseField<T>(node, "value");
    if (!value) {
        return Error(value.errorMessage());
    }
    Expected<const json *> easingNode = Child(node, "easing");
    if (!easingNode) {
        return Error(easingNode.errorMessage());
    }
    Expected<Easing> easing = EasingFromJson(**easingNode);
    if (!easing) {
        return Error(easing.errorMessage());
    }
    Keyframe<T> keyframe;
    keyframe.time = *time;
    keyframe.value = std::move(*value);
    keyframe.easing = *easing;
    if (const json *tangentNode = FindChild(node, "spatialInTangent")) {
        Expected<Vec2> tangent = Vec2FromJson(*tangentNode);
        if (!tangent) {
            return Error(tangent.errorMessage());
        }
        keyframe.spatialInTangent = *tangent;
    }
    if (const json *tangentNode = FindChild(node, "spatialOutTangent")) {
        Expected<Vec2> tangent = Vec2FromJson(*tangentNode);
        if (!tangent) {
            return Error(tangent.errorMessage());
        }
        keyframe.spatialOutTangent = *tangent;
    }
    return keyframe;
}

template <typename T>
Expected<void> AnimatableFromJson(const json &node, Animatable<T> &animatable) {
    if (const json *staticNode = FindChild(node, "static")) {
        Expected<T> value = FromJson<T>(*staticNode);
        if (!value) {
            return Error(value.errorMessage());
        }
        animatable.setStaticValue(std::move(*value));
    }
    if (const json *keyframesNode = FindChild(node, "keyframes")) {
        if (!keyframesNode->is_array()) {
            return Error("keyframes must be an array");
        }
        for (const json &keyframeNode : *keyframesNode) {
            Expected<Keyframe<T>> keyframe = KeyframeFromJson<T>(keyframeNode);
            if (!keyframe) {
                return Error(keyframe.errorMessage());
            }
            animatable.addKeyframe(std::move(*keyframe));
        }
    }
    return {};
}

// ---- Transform / Mask ----

json TransformToJson(const Transform &transform) {
    return {{"anchorPoint", AnimatableToJson(transform.anchorPoint)},
            {"position", AnimatableToJson(transform.position)},
            {"scale", AnimatableToJson(transform.scale)},
            {"rotation", AnimatableToJson(transform.rotation)},
            {"opacity", AnimatableToJson(transform.opacity)}};
}

Expected<void> TransformFromJson(const json &node, Transform &transform) {
    const char *fields[] = {"anchorPoint", "position", "scale", "rotation", "opacity"};
    Animatable<Vec2> *vec2Targets[] = {&transform.anchorPoint, &transform.position,
                                       &transform.scale};
    for (int i = 0; i < 3; ++i) {
        Expected<const json *> child = Child(node, fields[i]);
        if (!child) {
            return Error(child.errorMessage());
        }
        Expected<void> result = AnimatableFromJson(**child, *vec2Targets[i]);
        if (!result) {
            return Error(result.errorMessage());
        }
    }
    Expected<const json *> rotationNode = Child(node, fields[3]);
    if (!rotationNode) {
        return Error(rotationNode.errorMessage());
    }
    Expected<void> result = AnimatableFromJson(**rotationNode, transform.rotation);
    if (!result) {
        return Error(result.errorMessage());
    }
    Expected<const json *> opacityNode = Child(node, fields[4]);
    if (!opacityNode) {
        return Error(opacityNode.errorMessage());
    }
    return AnimatableFromJson(**opacityNode, transform.opacity);
}

json MaskToJson(const Mask &mask) {
    return {{"path", BezierPathToJson(mask.path)},
            {"mode", dto::ToString(mask.mode)},
            {"opacity", AnimatableToJson(mask.opacity)},
            {"inverted", mask.inverted}};
}

Expected<Mask> MaskFromJson(const json &node) {
    Expected<BezierPath> path = ParseField<BezierPath>(node, "path");
    if (!path) {
        return Error(path.errorMessage());
    }
    Expected<std::string> modeText = ParseField<std::string>(node, "mode");
    if (!modeText) {
        return Error(modeText.errorMessage());
    }
    Expected<MaskMode> mode = dto::maskModeFromString(*modeText);
    if (!mode) {
        return Error(mode.errorMessage());
    }
    Mask mask;
    mask.path = std::move(*path);
    mask.mode = *mode;
    Expected<const json *> opacityNode = Child(node, "opacity");
    if (!opacityNode) {
        return Error(opacityNode.errorMessage());
    }
    Expected<void> result = AnimatableFromJson(**opacityNode, mask.opacity);
    if (!result) {
        return Error(result.errorMessage());
    }
    Expected<bool> inverted = ParseField<bool>(node, "inverted");
    if (!inverted) {
        return Error(inverted.errorMessage());
    }
    mask.inverted = *inverted;
    return mask;
}

// ---- Shape elements (discriminant field "type") ----

json ShapesToJson(const std::vector<std::unique_ptr<ShapeElement>> &elements);

json ShapeToJson(const ShapeElement &element) {
    json node{{"id", IdToString(element.id)}, {"type", dto::ToString(element.type())}};
    switch (element.type()) {
        case ShapeType::Path: {
            const auto &shape = static_cast<const ShapePath &>(element);
            node["path"] = AnimatableToJson(shape.path);
            break;
        }
        case ShapeType::Fill: {
            const auto &shape = static_cast<const ShapeFill &>(element);
            node["color"] = AnimatableToJson(shape.color);
            node["opacity"] = AnimatableToJson(shape.opacity);
            node["fillRule"] = dto::ToString(shape.fillRule);
            break;
        }
        case ShapeType::Stroke: {
            const auto &shape = static_cast<const ShapeStroke &>(element);
            node["color"] = AnimatableToJson(shape.color);
            node["width"] = AnimatableToJson(shape.width);
            node["opacity"] = AnimatableToJson(shape.opacity);
            node["cap"] = dto::ToString(shape.cap);
            node["join"] = dto::ToString(shape.join);
            node["miterLimit"] = shape.miterLimit;
            break;
        }
        case ShapeType::Group: {
            const auto &shape = static_cast<const ShapeGroup &>(element);
            node["transform"] = TransformToJson(shape.transform);
            node["elements"] = ShapesToJson(shape.elements);
            break;
        }
        case ShapeType::Rect: {
            const auto &shape = static_cast<const ShapeRect &>(element);
            node["position"] = AnimatableToJson(shape.position);
            node["size"] = AnimatableToJson(shape.size);
            node["cornerRadius"] = AnimatableToJson(shape.cornerRadius);
            break;
        }
        case ShapeType::Ellipse: {
            const auto &shape = static_cast<const ShapeEllipse &>(element);
            node["position"] = AnimatableToJson(shape.position);
            node["size"] = AnimatableToJson(shape.size);
            break;
        }
        case ShapeType::TrimPath: {
            const auto &shape = static_cast<const ShapeTrimPath &>(element);
            node["start"] = AnimatableToJson(shape.start);
            node["end"] = AnimatableToJson(shape.end);
            node["offset"] = AnimatableToJson(shape.offset);
            break;
        }
    }
    return node;
}

json ShapesToJson(const std::vector<std::unique_ptr<ShapeElement>> &elements) {
    json nodes = json::array();
    for (const auto &element : elements) {
        nodes.push_back(ShapeToJson(*element));
    }
    return nodes;
}

Expected<std::unique_ptr<ShapeElement>> ShapeFromJson(const json &node) {
    Expected<std::string> typeText = ParseField<std::string>(node, "type");
    if (!typeText) {
        return Error(typeText.errorMessage());
    }
    Expected<ShapeType> type = dto::shapeTypeFromString(*typeText);
    if (!type) {
        return Error(type.errorMessage());
    }
    std::unique_ptr<ShapeElement> element;
    switch (*type) {
        case ShapeType::Path: {
            auto shape = std::make_unique<ShapePath>();
            Expected<const json *> pathNode = Child(node, "path");
            if (!pathNode) {
                return Error(pathNode.errorMessage());
            }
            Expected<void> result = AnimatableFromJson(**pathNode, shape->path);
            if (!result) {
                return Error(result.errorMessage());
            }
            element = std::move(shape);
            break;
        }
        case ShapeType::Fill: {
            auto shape = std::make_unique<ShapeFill>();
            Expected<const json *> colorNode = Child(node, "color");
            if (!colorNode) {
                return Error(colorNode.errorMessage());
            }
            Expected<void> result = AnimatableFromJson(**colorNode, shape->color);
            if (!result) {
                return Error(result.errorMessage());
            }
            Expected<const json *> opacityNode = Child(node, "opacity");
            if (!opacityNode) {
                return Error(opacityNode.errorMessage());
            }
            result = AnimatableFromJson(**opacityNode, shape->opacity);
            if (!result) {
                return Error(result.errorMessage());
            }
            Expected<std::string> ruleText = ParseField<std::string>(node, "fillRule");
            if (!ruleText) {
                return Error(ruleText.errorMessage());
            }
            Expected<FillRule> fillRule = dto::fillRuleFromString(*ruleText);
            if (!fillRule) {
                return Error(fillRule.errorMessage());
            }
            shape->fillRule = *fillRule;
            element = std::move(shape);
            break;
        }
        case ShapeType::Stroke: {
            auto shape = std::make_unique<ShapeStroke>();
            const char *animatableFields[] = {"color", "width", "opacity"};
            Animatable<Color> *colorTarget = &shape->color;
            Expected<const json *> colorNode = Child(node, animatableFields[0]);
            if (!colorNode) {
                return Error(colorNode.errorMessage());
            }
            Expected<void> result = AnimatableFromJson(**colorNode, *colorTarget);
            if (!result) {
                return Error(result.errorMessage());
            }
            Expected<const json *> widthNode = Child(node, animatableFields[1]);
            if (!widthNode) {
                return Error(widthNode.errorMessage());
            }
            result = AnimatableFromJson(**widthNode, shape->width);
            if (!result) {
                return Error(result.errorMessage());
            }
            Expected<const json *> opacityNode = Child(node, animatableFields[2]);
            if (!opacityNode) {
                return Error(opacityNode.errorMessage());
            }
            result = AnimatableFromJson(**opacityNode, shape->opacity);
            if (!result) {
                return Error(result.errorMessage());
            }
            Expected<std::string> capText = ParseField<std::string>(node, "cap");
            Expected<std::string> joinText = ParseField<std::string>(node, "join");
            Expected<float> miterLimit = ParseField<float>(node, "miterLimit");
            if (!capText || !joinText || !miterLimit) {
                return Error("Stroke is missing the cap/join/miterLimit fields");
            }
            Expected<LineCap> cap = dto::lineCapFromString(*capText);
            if (!cap) {
                return Error(cap.errorMessage());
            }
            Expected<LineJoin> join = dto::lineJoinFromString(*joinText);
            if (!join) {
                return Error(join.errorMessage());
            }
            shape->cap = *cap;
            shape->join = *join;
            shape->miterLimit = *miterLimit;
            element = std::move(shape);
            break;
        }
        case ShapeType::Group: {
            auto shape = std::make_unique<ShapeGroup>();
            Expected<const json *> transformNode = Child(node, "transform");
            if (!transformNode) {
                return Error(transformNode.errorMessage());
            }
            Expected<void> result = TransformFromJson(**transformNode, shape->transform);
            if (!result) {
                return Error(result.errorMessage());
            }
            const json *elementsNode = FindChild(node, "elements");
            if (!elementsNode || !elementsNode->is_array()) {
                return Error("Group is missing the elements array");
            }
            for (const json &childNode : *elementsNode) {
                Expected<std::unique_ptr<ShapeElement>> child = ShapeFromJson(childNode);
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
            Expected<const json *> positionNode = Child(node, "position");
            if (!positionNode) {
                return Error(positionNode.errorMessage());
            }
            Expected<void> result = AnimatableFromJson(**positionNode, shape->position);
            if (!result) {
                return Error(result.errorMessage());
            }
            Expected<const json *> sizeNode = Child(node, "size");
            if (!sizeNode) {
                return Error(sizeNode.errorMessage());
            }
            result = AnimatableFromJson(**sizeNode, shape->size);
            if (!result) {
                return Error(result.errorMessage());
            }
            Expected<const json *> radiusNode = Child(node, "cornerRadius");
            if (!radiusNode) {
                return Error(radiusNode.errorMessage());
            }
            result = AnimatableFromJson(**radiusNode, shape->cornerRadius);
            if (!result) {
                return Error(result.errorMessage());
            }
            element = std::move(shape);
            break;
        }
        case ShapeType::Ellipse: {
            auto shape = std::make_unique<ShapeEllipse>();
            Expected<const json *> positionNode = Child(node, "position");
            if (!positionNode) {
                return Error(positionNode.errorMessage());
            }
            Expected<void> result = AnimatableFromJson(**positionNode, shape->position);
            if (!result) {
                return Error(result.errorMessage());
            }
            Expected<const json *> sizeNode = Child(node, "size");
            if (!sizeNode) {
                return Error(sizeNode.errorMessage());
            }
            result = AnimatableFromJson(**sizeNode, shape->size);
            if (!result) {
                return Error(result.errorMessage());
            }
            element = std::move(shape);
            break;
        }
        case ShapeType::TrimPath: {
            auto shape = std::make_unique<ShapeTrimPath>();
            const char *trimFields[] = {"start", "end", "offset"};
            Expected<const json *> startNode = Child(node, trimFields[0]);
            if (!startNode) {
                return Error(startNode.errorMessage());
            }
            Expected<void> result = AnimatableFromJson(**startNode, shape->start);
            if (!result) {
                return Error(result.errorMessage());
            }
            Expected<const json *> endNode = Child(node, trimFields[1]);
            if (!endNode) {
                return Error(endNode.errorMessage());
            }
            result = AnimatableFromJson(**endNode, shape->end);
            if (!result) {
                return Error(result.errorMessage());
            }
            Expected<const json *> offsetNode = Child(node, trimFields[2]);
            if (!offsetNode) {
                return Error(offsetNode.errorMessage());
            }
            result = AnimatableFromJson(**offsetNode, shape->offset);
            if (!result) {
                return Error(result.errorMessage());
            }
            element = std::move(shape);
            break;
        }
    }
    Expected<EntityId> id = IdField(node, "id");
    if (!id) {
        return Error(id.errorMessage());
    }
    element->id = *id;
    return element;
}

// ---- LayerContent (discriminant field "type") ----

json ContentToJson(const LayerContent &content) {
    json node{{"type", dto::ToString(content.type())}};
    switch (content.type()) {
        case LayerType::Shape: {
            const auto &shape = static_cast<const ShapeContent &>(content);
            node["elements"] = ShapesToJson(shape.elements);
            break;
        }
        case LayerType::Image: {
            const auto &image = static_cast<const ImageContent &>(content);
            node["assetId"] = IdToString(image.assetId);
            break;
        }
        case LayerType::Text: {
            const auto &text = static_cast<const TextContent &>(content);
            node["text"] = AnimatableToJson(text.text);
            node["fontFamily"] = text.fontFamily;
            node["fontSize"] = AnimatableToJson(text.fontSize);
            break;
        }
        case LayerType::Null: {
            break;
        }
        case LayerType::Precomp: {
            const auto &precomp = static_cast<const PrecompContent &>(content);
            node["compositionId"] = IdToString(precomp.compositionId);
            break;
        }
    }
    return node;
}

Expected<std::unique_ptr<LayerContent>> ContentFromJson(const json &node) {
    Expected<std::string> typeText = ParseField<std::string>(node, "type");
    if (!typeText) {
        return Error(typeText.errorMessage());
    }
    Expected<LayerType> type = dto::layerTypeFromString(*typeText);
    if (!type) {
        return Error(type.errorMessage());
    }
    switch (*type) {
        case LayerType::Shape: {
            auto content = std::make_unique<ShapeContent>();
            const json *elementsNode = FindChild(node, "elements");
            if (!elementsNode || !elementsNode->is_array()) {
                return Error("Shape content is missing the elements array");
            }
            for (const json &elementNode : *elementsNode) {
                Expected<std::unique_ptr<ShapeElement>> element = ShapeFromJson(elementNode);
                if (!element) {
                    return Error(element.errorMessage());
                }
                content->elements.push_back(std::move(*element));
            }
            return std::unique_ptr<LayerContent>(std::move(content));
        }
        case LayerType::Image: {
            auto content = std::make_unique<ImageContent>();
            Expected<EntityId> assetId = IdField(node, "assetId");
            if (!assetId) {
                return Error(assetId.errorMessage());
            }
            content->assetId = *assetId;
            return std::unique_ptr<LayerContent>(std::move(content));
        }
        case LayerType::Text: {
            auto content = std::make_unique<TextContent>();
            Expected<const json *> textNode = Child(node, "text");
            if (!textNode) {
                return Error(textNode.errorMessage());
            }
            Expected<void> result = AnimatableFromJson(**textNode, content->text);
            if (!result) {
                return Error(result.errorMessage());
            }
            Expected<std::string> fontFamily = ParseField<std::string>(node, "fontFamily");
            if (!fontFamily) {
                return Error(fontFamily.errorMessage());
            }
            content->fontFamily = std::move(*fontFamily);
            Expected<const json *> fontSizeNode = Child(node, "fontSize");
            if (!fontSizeNode) {
                return Error(fontSizeNode.errorMessage());
            }
            result = AnimatableFromJson(**fontSizeNode, content->fontSize);
            if (!result) {
                return Error(result.errorMessage());
            }
            return std::unique_ptr<LayerContent>(std::move(content));
        }
        case LayerType::Null: {
            return std::unique_ptr<LayerContent>(std::make_unique<NullContent>());
        }
        case LayerType::Precomp: {
            auto content = std::make_unique<PrecompContent>();
            Expected<EntityId> compositionId = IdField(node, "compositionId");
            if (!compositionId) {
                return Error(compositionId.errorMessage());
            }
            content->compositionId = *compositionId;
            return std::unique_ptr<LayerContent>(std::move(content));
        }
    }
    return Error("unknown layer content type");
}

// ---- Layer / Composition / Asset / Document ----

json LayerToJson(const Layer &layer) {
    json node{{"id", IdToString(layer.id)},
              {"name", layer.name},
              {"type", dto::ToString(layer.type())},
              {"inPoint", layer.inPoint},
              {"outPoint", layer.outPoint},
              {"startTime", layer.startTime},
              {"timeStretch", layer.timeStretch},
              {"visible", layer.visible},
              {"locked", layer.locked},
              {"blendMode", dto::ToString(layer.blendMode)},
              {"transform", TransformToJson(layer.transform)},
              {"content", ContentToJson(*layer.content)}};
    if (layer.parentId.isValid()) {
        node["parentId"] = IdToString(layer.parentId);
    } else {
        node["parentId"] = nullptr;
    }
    json masks = json::array();
    for (const Mask &mask : layer.masks) {
        masks.push_back(MaskToJson(mask));
    }
    node["masks"] = std::move(masks);
    return node;
}

Expected<std::unique_ptr<Layer>> LayerFromJson(const json &node) {
    Expected<std::string> typeText = ParseField<std::string>(node, "type");
    if (!typeText) {
        return Error(typeText.errorMessage());
    }
    Expected<LayerType> type = dto::layerTypeFromString(*typeText);
    if (!type) {
        return Error(type.errorMessage());
    }
    auto layer = std::make_unique<Layer>(*type);

    Expected<EntityId> id = IdField(node, "id");
    if (!id) {
        return Error(id.errorMessage());
    }
    layer->id = *id;

    Expected<std::string> name = ParseField<std::string>(node, "name");
    if (!name) {
        return Error(name.errorMessage());
    }
    layer->name = std::move(*name);

    Expected<int64_t> inPoint = ParseField<int64_t>(node, "inPoint");
    Expected<int64_t> outPoint = ParseField<int64_t>(node, "outPoint");
    Expected<int64_t> startTime = ParseField<int64_t>(node, "startTime");
    Expected<double> timeStretch = ParseField<double>(node, "timeStretch");
    Expected<bool> visible = ParseField<bool>(node, "visible");
    Expected<bool> locked = ParseField<bool>(node, "locked");
    if (!inPoint || !outPoint || !startTime || !timeStretch || !visible || !locked) {
        return Error("Layer is missing the time/visibility fields");
    }
    layer->inPoint = *inPoint;
    layer->outPoint = *outPoint;
    layer->startTime = *startTime;
    layer->timeStretch = *timeStretch;
    layer->visible = *visible;
    layer->locked = *locked;

    Expected<std::string> blendText = ParseField<std::string>(node, "blendMode");
    if (!blendText) {
        return Error(blendText.errorMessage());
    }
    Expected<BlendMode> blendMode = dto::blendModeFromString(*blendText);
    if (!blendMode) {
        return Error(blendMode.errorMessage());
    }
    layer->blendMode = *blendMode;

    Expected<const json *> transformNode = Child(node, "transform");
    if (!transformNode) {
        return Error(transformNode.errorMessage());
    }
    Expected<void> result = TransformFromJson(**transformNode, layer->transform);
    if (!result) {
        return Error(result.errorMessage());
    }

    Expected<const json *> contentNode = Child(node, "content");
    if (!contentNode) {
        return Error(contentNode.errorMessage());
    }
    Expected<std::unique_ptr<LayerContent>> content = ContentFromJson(**contentNode);
    if (!content) {
        return Error(content.errorMessage());
    }
    layer->content = std::move(*content);

    const json *parentNode = FindChild(node, "parentId");
    if (parentNode && !parentNode->is_null()) {
        Expected<std::string> parentText = AsString(*parentNode);
        if (!parentText) {
            return Error(parentText.errorMessage());
        }
        Expected<EntityId> parentId = IdFromString(*parentText);
        if (!parentId) {
            return Error(parentId.errorMessage());
        }
        layer->parentId = *parentId;
    }

    if (const json *masksNode = FindChild(node, "masks")) {
        if (!masksNode->is_array()) {
            return Error("masks must be an array");
        }
        for (const json &maskNode : *masksNode) {
            Expected<Mask> mask = MaskFromJson(maskNode);
            if (!mask) {
                return Error(mask.errorMessage());
            }
            layer->masks.push_back(std::move(*mask));
        }
    }
    return layer;
}

json CompositionToJson(const Composition &composition) {
    json layers = json::array();
    for (const auto &layer : composition.layers) {
        layers.push_back(LayerToJson(*layer));
    }
    return {{"id", IdToString(composition.id)},
            {"name", composition.name},
            {"duration", composition.duration},
            {"frameRate",
             {{"num", composition.frameRate.num}, {"den", composition.frameRate.den}}},
            {"width", composition.width},
            {"height", composition.height},
            {"backgroundColor", ColorToJson(composition.backgroundColor)},
            {"layers", std::move(layers)}};
}

Expected<std::unique_ptr<Composition>> CompositionFromJson(const json &node) {
    auto composition = std::make_unique<Composition>();

    Expected<EntityId> id = IdField(node, "id");
    if (!id) {
        return Error(id.errorMessage());
    }
    composition->id = *id;

    Expected<std::string> name = ParseField<std::string>(node, "name");
    if (!name) {
        return Error(name.errorMessage());
    }
    composition->name = std::move(*name);

    Expected<int64_t> duration = ParseField<int64_t>(node, "duration");
    if (!duration) {
        return Error(duration.errorMessage());
    }
    composition->duration = *duration;

    Expected<const json *> frameRateNode = Child(node, "frameRate");
    if (!frameRateNode) {
        return Error(frameRateNode.errorMessage());
    }
    Expected<uint32_t> num = ParseField<uint32_t>(**frameRateNode, "num");
    Expected<uint32_t> den = ParseField<uint32_t>(**frameRateNode, "den");
    if (!num || !den) {
        return Error("frameRate is missing the num/den fields");
    }
    composition->frameRate = {*num, *den};

    Expected<int> width = ParseField<int>(node, "width");
    Expected<int> height = ParseField<int>(node, "height");
    if (!width || !height) {
        return Error("Composition is missing the width/height fields");
    }
    composition->width = *width;
    composition->height = *height;

    Expected<Color> backgroundColor = ParseField<Color>(node, "backgroundColor");
    if (!backgroundColor) {
        return Error(backgroundColor.errorMessage());
    }
    composition->backgroundColor = *backgroundColor;

    const json *layersNode = FindChild(node, "layers");
    if (!layersNode || !layersNode->is_array()) {
        return Error("Composition is missing the layers array");
    }
    for (const json &layerNode : *layersNode) {
        Expected<std::unique_ptr<Layer>> layer = LayerFromJson(layerNode);
        if (!layer) {
            return Error(layer.errorMessage());
        }
        composition->layers.push_back(std::move(*layer));
    }
    return composition;
}

json AssetToJson(const Asset &asset) {
    return {{"id", IdToString(asset.id)},
            {"type", dto::ToString(asset.type)},
            {"name", asset.name},
            {"path", asset.path}};
}

Expected<Asset> AssetFromJson(const json &node) {
    Asset asset;
    Expected<EntityId> id = IdField(node, "id");
    if (!id) {
        return Error(id.errorMessage());
    }
    asset.id = *id;
    Expected<std::string> typeText = ParseField<std::string>(node, "type");
    if (!typeText) {
        return Error(typeText.errorMessage());
    }
    Expected<AssetType> type = dto::assetTypeFromString(*typeText);
    if (!type) {
        return Error(type.errorMessage());
    }
    asset.type = *type;
    Expected<std::string> name = ParseField<std::string>(node, "name");
    Expected<std::string> path = ParseField<std::string>(node, "path");
    if (!name || !path) {
        return Error("Asset is missing the name/path fields");
    }
    asset.name = std::move(*name);
    asset.path = std::move(*path);
    return asset;
}

json DocumentToJson(const Document &document) {
    json compositions = json::array();
    for (const auto &composition : document.compositions) {
        compositions.push_back(CompositionToJson(*composition));
    }
    json assets = json::array();
    for (const Asset &asset : document.assets) {
        assets.push_back(AssetToJson(asset));
    }
    return {{"schemaVersion", dto::SCHEMA_VERSION},
            {"id", IdToString(document.id)},
            {"name", document.name},
            {"assets", std::move(assets)},
            {"compositions", std::move(compositions)}};
}

}  // namespace

std::string Serializer::serialize(const Document &document) {
    return DocumentToJson(document).dump(2);
}

Expected<std::unique_ptr<Document>> Serializer::deserialize(const std::string &jsonText) {
    Expected<std::string> migrated = SchemaMigrator::migrate(jsonText);
    if (!migrated) {
        return Error(migrated.errorMessage());
    }
    const json data = json::parse(*migrated, nullptr, false);
    if (data.is_discarded() || !data.is_object()) {
        return Error("failed to parse the document JSON");
    }

    auto document = std::make_unique<Document>();

    Expected<EntityId> id = IdField(data, "id");
    if (!id) {
        return Error(id.errorMessage());
    }
    document->id = *id;

    Expected<std::string> name = ParseField<std::string>(data, "name");
    if (!name) {
        return Error(name.errorMessage());
    }
    document->name = std::move(*name);

    const json *assetsNode = FindChild(data, "assets");
    if (!assetsNode || !assetsNode->is_array()) {
        return Error("missing the assets array");
    }
    for (const json &assetNode : *assetsNode) {
        Expected<Asset> asset = AssetFromJson(assetNode);
        if (!asset) {
            return Error(asset.errorMessage());
        }
        document->assets.push_back(std::move(*asset));
    }

    const json *compositionsNode = FindChild(data, "compositions");
    if (!compositionsNode || !compositionsNode->is_array()) {
        return Error("missing the compositions array");
    }
    for (const json &compositionNode : *compositionsNode) {
        Expected<std::unique_ptr<Composition>> composition =
            CompositionFromJson(compositionNode);
        if (!composition) {
            return Error(composition.errorMessage());
        }
        document->compositions.push_back(std::move(*composition));
    }

    document->refreshEntityIndex();
    return document;
}

uint64_t DocumentFingerprint(const Document &document) {
    const std::string compact = DocumentToJson(document).dump();
    uint64_t hash = 14695981039346656037ull;  // FNV-1a 64
    for (const unsigned char byte : compact) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

}  // namespace motion
