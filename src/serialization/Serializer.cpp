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

Expected<const json *, ParseError> Child(const json &node, const char *key) {
    const json *child = FindChild(node, key);
    if (!child) {
        return Unexpected(ParseError(std::string("missing field: ") + key));
    }
    return child;
}

Expected<float, ParseError> AsFloat(const json &node) {
    if (!node.is_number()) {
        return Unexpected(ParseError("field is not a number"));
    }
    return node.get<float>();
}

Expected<double, ParseError> AsDouble(const json &node) {
    if (!node.is_number()) {
        return Unexpected(ParseError("field is not a number"));
    }
    return node.get<double>();
}

Expected<int64_t, ParseError> AsInt64(const json &node) {
    if (!node.is_number_integer()) {
        return Unexpected(ParseError("field is not an integer"));
    }
    return node.get<int64_t>();
}

Expected<int, ParseError> AsInt(const json &node) {
    Expected<int64_t, ParseError> value = AsInt64(node);
    if (!value) {
        return Unexpected(value.error());
    }
    return int(*value);
}

Expected<uint32_t, ParseError> AsUint32(const json &node) {
    Expected<int64_t, ParseError> value = AsInt64(node);
    if (!value || *value < 0 || *value > int64_t(UINT32_MAX)) {
        return Unexpected(ParseError("field is not a valid unsigned integer"));
    }
    return uint32_t(*value);
}

Expected<bool, ParseError> AsBool(const json &node) {
    if (!node.is_boolean()) {
        return Unexpected(ParseError("field is not a boolean"));
    }
    return node.get<bool>();
}

Expected<std::string, ParseError> AsString(const json &node) {
    if (!node.is_string()) {
        return Unexpected(ParseError("field is not a string"));
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

Expected<EntityId, ParseError> IdFromString(const std::string &text) {
    uint64_t value = 0;
    const char *begin = text.data();
    const char *end = begin + text.size();
    const auto result = std::from_chars(begin, end, value, 16);
    if (result.ec != std::errc() || result.ptr != end || value == 0) {
        return Unexpected(ParseError("invalid entity id: " + text));
    }
    return EntityId{value};
}

Expected<EntityId, ParseError> IdField(const json &node, const char *key) {
    Expected<const json *, ParseError> child = Child(node, key);
    if (!child) {
        return Unexpected(child.error());
    }
    Expected<std::string, ParseError> text = AsString(**child);
    if (!text) {
        return Unexpected(text.error());
    }
    return IdFromString(*text);
}

// ---- Leaf values ----

json Vec2ToJson(Vec2 value) {
    return json::array({value.x, value.y});
}

Expected<Vec2, ParseError> Vec2FromJson(const json &node) {
    if (!node.is_array() || node.size() != 2) {
        return Unexpected(ParseError("Vec2 must be a 2-element array"));
    }
    Expected<float, ParseError> x = AsFloat(node[0]);
    if (!x) {
        return Unexpected(x.error());
    }
    Expected<float, ParseError> y = AsFloat(node[1]);
    if (!y) {
        return Unexpected(y.error());
    }
    return Vec2{*x, *y};
}

json ColorToJson(Color value) {
    return json::array({value.r, value.g, value.b, value.a});
}

Expected<Color, ParseError> ColorFromJson(const json &node) {
    if (!node.is_array() || node.size() != 4) {
        return Unexpected(ParseError("Color must be a 4-element array"));
    }
    Expected<float, ParseError> r = AsFloat(node[0]);
    Expected<float, ParseError> g = AsFloat(node[1]);
    Expected<float, ParseError> b = AsFloat(node[2]);
    Expected<float, ParseError> a = AsFloat(node[3]);
    if (!r || !g || !b || !a) {
        return Unexpected(ParseError("Color component is not a number"));
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

Expected<BezierPath, ParseError> BezierPathFromJson(const json &node) {
    Expected<const json *, ParseError> closedNode = Child(node, "closed");
    if (!closedNode) {
        return Unexpected(closedNode.error());
    }
    Expected<bool, ParseError> closed = AsBool(**closedNode);
    if (!closed) {
        return Unexpected(closed.error());
    }
    const json *verticesNode = FindChild(node, "vertices");
    if (!verticesNode || !verticesNode->is_array()) {
        return Unexpected(ParseError("missing the vertices array"));
    }
    BezierPath path;
    path.closed = *closed;
    for (const json &vertexNode : *verticesNode) {
        Expected<const json *, ParseError> pointNode = Child(vertexNode, "point");
        Expected<const json *, ParseError> inNode = Child(vertexNode, "inTangent");
        Expected<const json *, ParseError> outNode = Child(vertexNode, "outTangent");
        if (!pointNode || !inNode || !outNode) {
            return Unexpected(ParseError("vertex is missing a tangent field"));
        }
        Expected<Vec2, ParseError> point = Vec2FromJson(**pointNode);
        Expected<Vec2, ParseError> inTangent = Vec2FromJson(**inNode);
        Expected<Vec2, ParseError> outTangent = Vec2FromJson(**outNode);
        if (!point || !inTangent || !outTangent) {
            return Unexpected(ParseError("invalid vertex coordinates"));
        }
        path.vertices.push_back({*point, *inTangent, *outTangent});
    }
    return path;
}

template <typename T>
Expected<T, ParseError> FromJson(const json &node);

template <>
Expected<float, ParseError> FromJson<float>(const json &node) {
    return AsFloat(node);
}
template <>
Expected<double, ParseError> FromJson<double>(const json &node) {
    return AsDouble(node);
}
template <>
Expected<int, ParseError> FromJson<int>(const json &node) {
    return AsInt(node);
}
template <>
Expected<int64_t, ParseError> FromJson<int64_t>(const json &node) {
    return AsInt64(node);
}
template <>
Expected<uint32_t, ParseError> FromJson<uint32_t>(const json &node) {
    return AsUint32(node);
}
template <>
Expected<bool, ParseError> FromJson<bool>(const json &node) {
    return AsBool(node);
}
template <>
Expected<std::string, ParseError> FromJson<std::string>(const json &node) {
    return AsString(node);
}
template <>
Expected<Vec2, ParseError> FromJson<Vec2>(const json &node) {
    return Vec2FromJson(node);
}
template <>
Expected<Color, ParseError> FromJson<Color>(const json &node) {
    return ColorFromJson(node);
}
template <>
Expected<BezierPath, ParseError> FromJson<BezierPath>(const json &node) {
    return BezierPathFromJson(node);
}

template <typename T>
Expected<T, ParseError> ParseField(const json &node, const char *key) {
    Expected<const json *, ParseError> child = Child(node, key);
    if (!child) {
        return Unexpected(child.error());
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

Expected<Easing, ParseError> EasingFromJson(const json &node) {
    Expected<std::string, ParseError> typeText = ParseField<std::string>(node, "type");
    if (!typeText) {
        return Unexpected(typeText.error());
    }
    Expected<Easing::Type, ParseError> type = dto::easingTypeFromString(*typeText);
    if (!type) {
        return Unexpected(type.error());
    }
    if (*type != Easing::Type::Bezier) {
        return *type == Easing::Type::Hold ? Easing::Hold() : Easing::Linear();
    }
    Expected<float, ParseError> inX = ParseField<float>(node, "inX");
    Expected<float, ParseError> inY = ParseField<float>(node, "inY");
    Expected<float, ParseError> outX = ParseField<float>(node, "outX");
    Expected<float, ParseError> outY = ParseField<float>(node, "outY");
    if (!inX || !inY || !outX || !outY) {
        return Unexpected(ParseError("Bezier easing is missing control point fields"));
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
Expected<Keyframe<T>, ParseError> KeyframeFromJson(const json &node) {
    Expected<int64_t, ParseError> time = ParseField<int64_t>(node, "time");
    if (!time) {
        return Unexpected(time.error());
    }
    Expected<T, ParseError> value = ParseField<T>(node, "value");
    if (!value) {
        return Unexpected(value.error());
    }
    Expected<const json *, ParseError> easingNode = Child(node, "easing");
    if (!easingNode) {
        return Unexpected(easingNode.error());
    }
    Expected<Easing, ParseError> easing = EasingFromJson(**easingNode);
    if (!easing) {
        return Unexpected(easing.error());
    }
    Keyframe<T> keyframe;
    keyframe.time = *time;
    keyframe.value = std::move(*value);
    keyframe.easing = *easing;
    if (const json *tangentNode = FindChild(node, "spatialInTangent")) {
        Expected<Vec2, ParseError> tangent = Vec2FromJson(*tangentNode);
        if (!tangent) {
            return Unexpected(tangent.error());
        }
        keyframe.spatialInTangent = *tangent;
    }
    if (const json *tangentNode = FindChild(node, "spatialOutTangent")) {
        Expected<Vec2, ParseError> tangent = Vec2FromJson(*tangentNode);
        if (!tangent) {
            return Unexpected(tangent.error());
        }
        keyframe.spatialOutTangent = *tangent;
    }
    return keyframe;
}

template <typename T>
Expected<void, ParseError> AnimatableFromJson(const json &node, Animatable<T> &animatable) {
    if (const json *staticNode = FindChild(node, "static")) {
        Expected<T, ParseError> value = FromJson<T>(*staticNode);
        if (!value) {
            return Unexpected(value.error());
        }
        animatable.setStaticValue(std::move(*value));
    }
    if (const json *keyframesNode = FindChild(node, "keyframes")) {
        if (!keyframesNode->is_array()) {
            return Unexpected(ParseError("keyframes must be an array"));
        }
        for (const json &keyframeNode : *keyframesNode) {
            Expected<Keyframe<T>, ParseError> keyframe = KeyframeFromJson<T>(keyframeNode);
            if (!keyframe) {
                return Unexpected(keyframe.error());
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

Expected<void, ParseError> TransformFromJson(const json &node, Transform &transform) {
    const char *fields[] = {"anchorPoint", "position", "scale", "rotation", "opacity"};
    Animatable<Vec2> *vec2Targets[] = {&transform.anchorPoint, &transform.position,
                                       &transform.scale};
    for (int i = 0; i < 3; ++i) {
        Expected<const json *, ParseError> child = Child(node, fields[i]);
        if (!child) {
            return Unexpected(child.error());
        }
        Expected<void, ParseError> result = AnimatableFromJson(**child, *vec2Targets[i]);
        if (!result) {
            return Unexpected(result.error());
        }
    }
    Expected<const json *, ParseError> rotationNode = Child(node, fields[3]);
    if (!rotationNode) {
        return Unexpected(rotationNode.error());
    }
    Expected<void, ParseError> result = AnimatableFromJson(**rotationNode, transform.rotation);
    if (!result) {
        return Unexpected(result.error());
    }
    Expected<const json *, ParseError> opacityNode = Child(node, fields[4]);
    if (!opacityNode) {
        return Unexpected(opacityNode.error());
    }
    return AnimatableFromJson(**opacityNode, transform.opacity);
}

json MaskToJson(const Mask &mask) {
    return {{"path", BezierPathToJson(mask.path)},
            {"mode", dto::ToString(mask.mode)},
            {"opacity", AnimatableToJson(mask.opacity)},
            {"inverted", mask.inverted}};
}

Expected<Mask, ParseError> MaskFromJson(const json &node) {
    Expected<BezierPath, ParseError> path = ParseField<BezierPath>(node, "path");
    if (!path) {
        return Unexpected(path.error());
    }
    Expected<std::string, ParseError> modeText = ParseField<std::string>(node, "mode");
    if (!modeText) {
        return Unexpected(modeText.error());
    }
    Expected<MaskMode, ParseError> mode = dto::maskModeFromString(*modeText);
    if (!mode) {
        return Unexpected(mode.error());
    }
    Mask mask;
    mask.path = std::move(*path);
    mask.mode = *mode;
    Expected<const json *, ParseError> opacityNode = Child(node, "opacity");
    if (!opacityNode) {
        return Unexpected(opacityNode.error());
    }
    Expected<void, ParseError> result = AnimatableFromJson(**opacityNode, mask.opacity);
    if (!result) {
        return Unexpected(result.error());
    }
    Expected<bool, ParseError> inverted = ParseField<bool>(node, "inverted");
    if (!inverted) {
        return Unexpected(inverted.error());
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

Expected<std::unique_ptr<ShapeElement>, ParseError> ShapeFromJson(const json &node) {
    Expected<std::string, ParseError> typeText = ParseField<std::string>(node, "type");
    if (!typeText) {
        return Unexpected(typeText.error());
    }
    Expected<ShapeType, ParseError> type = dto::shapeTypeFromString(*typeText);
    if (!type) {
        return Unexpected(type.error());
    }
    std::unique_ptr<ShapeElement> element;
    switch (*type) {
        case ShapeType::Path: {
            auto shape = std::make_unique<ShapePath>();
            Expected<const json *, ParseError> pathNode = Child(node, "path");
            if (!pathNode) {
                return Unexpected(pathNode.error());
            }
            Expected<void, ParseError> result = AnimatableFromJson(**pathNode, shape->path);
            if (!result) {
                return Unexpected(result.error());
            }
            element = std::move(shape);
            break;
        }
        case ShapeType::Fill: {
            auto shape = std::make_unique<ShapeFill>();
            Expected<const json *, ParseError> colorNode = Child(node, "color");
            if (!colorNode) {
                return Unexpected(colorNode.error());
            }
            Expected<void, ParseError> result = AnimatableFromJson(**colorNode, shape->color);
            if (!result) {
                return Unexpected(result.error());
            }
            Expected<const json *, ParseError> opacityNode = Child(node, "opacity");
            if (!opacityNode) {
                return Unexpected(opacityNode.error());
            }
            result = AnimatableFromJson(**opacityNode, shape->opacity);
            if (!result) {
                return Unexpected(result.error());
            }
            Expected<std::string, ParseError> ruleText = ParseField<std::string>(node, "fillRule");
            if (!ruleText) {
                return Unexpected(ruleText.error());
            }
            Expected<FillRule, ParseError> fillRule = dto::fillRuleFromString(*ruleText);
            if (!fillRule) {
                return Unexpected(fillRule.error());
            }
            shape->fillRule = *fillRule;
            element = std::move(shape);
            break;
        }
        case ShapeType::Stroke: {
            auto shape = std::make_unique<ShapeStroke>();
            const char *animatableFields[] = {"color", "width", "opacity"};
            Animatable<Color> *colorTarget = &shape->color;
            Expected<const json *, ParseError> colorNode = Child(node, animatableFields[0]);
            if (!colorNode) {
                return Unexpected(colorNode.error());
            }
            Expected<void, ParseError> result = AnimatableFromJson(**colorNode, *colorTarget);
            if (!result) {
                return Unexpected(result.error());
            }
            Expected<const json *, ParseError> widthNode = Child(node, animatableFields[1]);
            if (!widthNode) {
                return Unexpected(widthNode.error());
            }
            result = AnimatableFromJson(**widthNode, shape->width);
            if (!result) {
                return Unexpected(result.error());
            }
            Expected<const json *, ParseError> opacityNode = Child(node, animatableFields[2]);
            if (!opacityNode) {
                return Unexpected(opacityNode.error());
            }
            result = AnimatableFromJson(**opacityNode, shape->opacity);
            if (!result) {
                return Unexpected(result.error());
            }
            Expected<std::string, ParseError> capText = ParseField<std::string>(node, "cap");
            Expected<std::string, ParseError> joinText = ParseField<std::string>(node, "join");
            Expected<float, ParseError> miterLimit = ParseField<float>(node, "miterLimit");
            if (!capText || !joinText || !miterLimit) {
                return Unexpected(ParseError("Stroke is missing the cap/join/miterLimit fields"));
            }
            Expected<LineCap, ParseError> cap = dto::lineCapFromString(*capText);
            if (!cap) {
                return Unexpected(cap.error());
            }
            Expected<LineJoin, ParseError> join = dto::lineJoinFromString(*joinText);
            if (!join) {
                return Unexpected(join.error());
            }
            shape->cap = *cap;
            shape->join = *join;
            shape->miterLimit = *miterLimit;
            element = std::move(shape);
            break;
        }
        case ShapeType::Group: {
            auto shape = std::make_unique<ShapeGroup>();
            Expected<const json *, ParseError> transformNode = Child(node, "transform");
            if (!transformNode) {
                return Unexpected(transformNode.error());
            }
            Expected<void, ParseError> result = TransformFromJson(**transformNode, shape->transform);
            if (!result) {
                return Unexpected(result.error());
            }
            const json *elementsNode = FindChild(node, "elements");
            if (!elementsNode || !elementsNode->is_array()) {
                return Unexpected(ParseError("Group is missing the elements array"));
            }
            for (const json &childNode : *elementsNode) {
                Expected<std::unique_ptr<ShapeElement>, ParseError> child = ShapeFromJson(childNode);
                if (!child) {
                    return Unexpected(child.error());
                }
                shape->elements.push_back(std::move(*child));
            }
            element = std::move(shape);
            break;
        }
        case ShapeType::Rect: {
            auto shape = std::make_unique<ShapeRect>();
            Expected<const json *, ParseError> positionNode = Child(node, "position");
            if (!positionNode) {
                return Unexpected(positionNode.error());
            }
            Expected<void, ParseError> result = AnimatableFromJson(**positionNode, shape->position);
            if (!result) {
                return Unexpected(result.error());
            }
            Expected<const json *, ParseError> sizeNode = Child(node, "size");
            if (!sizeNode) {
                return Unexpected(sizeNode.error());
            }
            result = AnimatableFromJson(**sizeNode, shape->size);
            if (!result) {
                return Unexpected(result.error());
            }
            Expected<const json *, ParseError> radiusNode = Child(node, "cornerRadius");
            if (!radiusNode) {
                return Unexpected(radiusNode.error());
            }
            result = AnimatableFromJson(**radiusNode, shape->cornerRadius);
            if (!result) {
                return Unexpected(result.error());
            }
            element = std::move(shape);
            break;
        }
        case ShapeType::Ellipse: {
            auto shape = std::make_unique<ShapeEllipse>();
            Expected<const json *, ParseError> positionNode = Child(node, "position");
            if (!positionNode) {
                return Unexpected(positionNode.error());
            }
            Expected<void, ParseError> result = AnimatableFromJson(**positionNode, shape->position);
            if (!result) {
                return Unexpected(result.error());
            }
            Expected<const json *, ParseError> sizeNode = Child(node, "size");
            if (!sizeNode) {
                return Unexpected(sizeNode.error());
            }
            result = AnimatableFromJson(**sizeNode, shape->size);
            if (!result) {
                return Unexpected(result.error());
            }
            element = std::move(shape);
            break;
        }
        case ShapeType::TrimPath: {
            auto shape = std::make_unique<ShapeTrimPath>();
            const char *trimFields[] = {"start", "end", "offset"};
            Expected<const json *, ParseError> startNode = Child(node, trimFields[0]);
            if (!startNode) {
                return Unexpected(startNode.error());
            }
            Expected<void, ParseError> result = AnimatableFromJson(**startNode, shape->start);
            if (!result) {
                return Unexpected(result.error());
            }
            Expected<const json *, ParseError> endNode = Child(node, trimFields[1]);
            if (!endNode) {
                return Unexpected(endNode.error());
            }
            result = AnimatableFromJson(**endNode, shape->end);
            if (!result) {
                return Unexpected(result.error());
            }
            Expected<const json *, ParseError> offsetNode = Child(node, trimFields[2]);
            if (!offsetNode) {
                return Unexpected(offsetNode.error());
            }
            result = AnimatableFromJson(**offsetNode, shape->offset);
            if (!result) {
                return Unexpected(result.error());
            }
            element = std::move(shape);
            break;
        }
    }
    Expected<EntityId, ParseError> id = IdField(node, "id");
    if (!id) {
        return Unexpected(id.error());
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

Expected<std::unique_ptr<LayerContent>, ParseError> ContentFromJson(const json &node) {
    Expected<std::string, ParseError> typeText = ParseField<std::string>(node, "type");
    if (!typeText) {
        return Unexpected(typeText.error());
    }
    Expected<LayerType, ParseError> type = dto::layerTypeFromString(*typeText);
    if (!type) {
        return Unexpected(type.error());
    }
    switch (*type) {
        case LayerType::Shape: {
            auto content = std::make_unique<ShapeContent>();
            const json *elementsNode = FindChild(node, "elements");
            if (!elementsNode || !elementsNode->is_array()) {
                return Unexpected(ParseError("Shape content is missing the elements array"));
            }
            for (const json &elementNode : *elementsNode) {
                Expected<std::unique_ptr<ShapeElement>, ParseError> element = ShapeFromJson(elementNode);
                if (!element) {
                    return Unexpected(element.error());
                }
                content->elements.push_back(std::move(*element));
            }
            return std::unique_ptr<LayerContent>(std::move(content));
        }
        case LayerType::Image: {
            auto content = std::make_unique<ImageContent>();
            Expected<EntityId, ParseError> assetId = IdField(node, "assetId");
            if (!assetId) {
                return Unexpected(assetId.error());
            }
            content->assetId = *assetId;
            return std::unique_ptr<LayerContent>(std::move(content));
        }
        case LayerType::Text: {
            auto content = std::make_unique<TextContent>();
            Expected<const json *, ParseError> textNode = Child(node, "text");
            if (!textNode) {
                return Unexpected(textNode.error());
            }
            Expected<void, ParseError> result = AnimatableFromJson(**textNode, content->text);
            if (!result) {
                return Unexpected(result.error());
            }
            Expected<std::string, ParseError> fontFamily = ParseField<std::string>(node, "fontFamily");
            if (!fontFamily) {
                return Unexpected(fontFamily.error());
            }
            content->fontFamily = std::move(*fontFamily);
            Expected<const json *, ParseError> fontSizeNode = Child(node, "fontSize");
            if (!fontSizeNode) {
                return Unexpected(fontSizeNode.error());
            }
            result = AnimatableFromJson(**fontSizeNode, content->fontSize);
            if (!result) {
                return Unexpected(result.error());
            }
            return std::unique_ptr<LayerContent>(std::move(content));
        }
        case LayerType::Null: {
            return std::unique_ptr<LayerContent>(std::make_unique<NullContent>());
        }
        case LayerType::Precomp: {
            auto content = std::make_unique<PrecompContent>();
            Expected<EntityId, ParseError> compositionId = IdField(node, "compositionId");
            if (!compositionId) {
                return Unexpected(compositionId.error());
            }
            content->compositionId = *compositionId;
            return std::unique_ptr<LayerContent>(std::move(content));
        }
    }
    return Unexpected(ParseError("unknown layer content type"));
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

Expected<std::unique_ptr<Layer>, ParseError> LayerFromJson(const json &node) {
    Expected<std::string, ParseError> typeText = ParseField<std::string>(node, "type");
    if (!typeText) {
        return Unexpected(typeText.error());
    }
    Expected<LayerType, ParseError> type = dto::layerTypeFromString(*typeText);
    if (!type) {
        return Unexpected(type.error());
    }
    auto layer = std::make_unique<Layer>(*type);

    Expected<EntityId, ParseError> id = IdField(node, "id");
    if (!id) {
        return Unexpected(id.error());
    }
    layer->id = *id;

    Expected<std::string, ParseError> name = ParseField<std::string>(node, "name");
    if (!name) {
        return Unexpected(name.error());
    }
    layer->name = std::move(*name);

    Expected<int64_t, ParseError> inPoint = ParseField<int64_t>(node, "inPoint");
    Expected<int64_t, ParseError> outPoint = ParseField<int64_t>(node, "outPoint");
    Expected<int64_t, ParseError> startTime = ParseField<int64_t>(node, "startTime");
    Expected<double, ParseError> timeStretch = ParseField<double>(node, "timeStretch");
    Expected<bool, ParseError> visible = ParseField<bool>(node, "visible");
    Expected<bool, ParseError> locked = ParseField<bool>(node, "locked");
    if (!inPoint || !outPoint || !startTime || !timeStretch || !visible || !locked) {
        return Unexpected(ParseError("Layer is missing the time/visibility fields"));
    }
    layer->inPoint = *inPoint;
    layer->outPoint = *outPoint;
    layer->startTime = *startTime;
    layer->timeStretch = *timeStretch;
    layer->visible = *visible;
    layer->locked = *locked;

    Expected<std::string, ParseError> blendText = ParseField<std::string>(node, "blendMode");
    if (!blendText) {
        return Unexpected(blendText.error());
    }
    Expected<BlendMode, ParseError> blendMode = dto::blendModeFromString(*blendText);
    if (!blendMode) {
        return Unexpected(blendMode.error());
    }
    layer->blendMode = *blendMode;

    Expected<const json *, ParseError> transformNode = Child(node, "transform");
    if (!transformNode) {
        return Unexpected(transformNode.error());
    }
    Expected<void, ParseError> result = TransformFromJson(**transformNode, layer->transform);
    if (!result) {
        return Unexpected(result.error());
    }

    Expected<const json *, ParseError> contentNode = Child(node, "content");
    if (!contentNode) {
        return Unexpected(contentNode.error());
    }
    Expected<std::unique_ptr<LayerContent>, ParseError> content = ContentFromJson(**contentNode);
    if (!content) {
        return Unexpected(content.error());
    }
    layer->content = std::move(*content);

    const json *parentNode = FindChild(node, "parentId");
    if (parentNode && !parentNode->is_null()) {
        Expected<std::string, ParseError> parentText = AsString(*parentNode);
        if (!parentText) {
            return Unexpected(parentText.error());
        }
        Expected<EntityId, ParseError> parentId = IdFromString(*parentText);
        if (!parentId) {
            return Unexpected(parentId.error());
        }
        layer->parentId = *parentId;
    }

    if (const json *masksNode = FindChild(node, "masks")) {
        if (!masksNode->is_array()) {
            return Unexpected(ParseError("masks must be an array"));
        }
        for (const json &maskNode : *masksNode) {
            Expected<Mask, ParseError> mask = MaskFromJson(maskNode);
            if (!mask) {
                return Unexpected(mask.error());
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

Expected<std::unique_ptr<Composition>, ParseError> CompositionFromJson(const json &node) {
    auto composition = std::make_unique<Composition>();

    Expected<EntityId, ParseError> id = IdField(node, "id");
    if (!id) {
        return Unexpected(id.error());
    }
    composition->id = *id;

    Expected<std::string, ParseError> name = ParseField<std::string>(node, "name");
    if (!name) {
        return Unexpected(name.error());
    }
    composition->name = std::move(*name);

    Expected<int64_t, ParseError> duration = ParseField<int64_t>(node, "duration");
    if (!duration) {
        return Unexpected(duration.error());
    }
    composition->duration = *duration;

    Expected<const json *, ParseError> frameRateNode = Child(node, "frameRate");
    if (!frameRateNode) {
        return Unexpected(frameRateNode.error());
    }
    Expected<uint32_t, ParseError> num = ParseField<uint32_t>(**frameRateNode, "num");
    Expected<uint32_t, ParseError> den = ParseField<uint32_t>(**frameRateNode, "den");
    if (!num || !den) {
        return Unexpected(ParseError("frameRate is missing the num/den fields"));
    }
    composition->frameRate = {*num, *den};

    Expected<int, ParseError> width = ParseField<int>(node, "width");
    Expected<int, ParseError> height = ParseField<int>(node, "height");
    if (!width || !height) {
        return Unexpected(ParseError("Composition is missing the width/height fields"));
    }
    composition->width = *width;
    composition->height = *height;

    Expected<Color, ParseError> backgroundColor = ParseField<Color>(node, "backgroundColor");
    if (!backgroundColor) {
        return Unexpected(backgroundColor.error());
    }
    composition->backgroundColor = *backgroundColor;

    const json *layersNode = FindChild(node, "layers");
    if (!layersNode || !layersNode->is_array()) {
        return Unexpected(ParseError("Composition is missing the layers array"));
    }
    for (const json &layerNode : *layersNode) {
        Expected<std::unique_ptr<Layer>, ParseError> layer = LayerFromJson(layerNode);
        if (!layer) {
            return Unexpected(layer.error());
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

Expected<Asset, ParseError> AssetFromJson(const json &node) {
    Asset asset;
    Expected<EntityId, ParseError> id = IdField(node, "id");
    if (!id) {
        return Unexpected(id.error());
    }
    asset.id = *id;
    Expected<std::string, ParseError> typeText = ParseField<std::string>(node, "type");
    if (!typeText) {
        return Unexpected(typeText.error());
    }
    Expected<AssetType, ParseError> type = dto::assetTypeFromString(*typeText);
    if (!type) {
        return Unexpected(type.error());
    }
    asset.type = *type;
    Expected<std::string, ParseError> name = ParseField<std::string>(node, "name");
    Expected<std::string, ParseError> path = ParseField<std::string>(node, "path");
    if (!name || !path) {
        return Unexpected(ParseError("Asset is missing the name/path fields"));
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

Expected<std::unique_ptr<Document>, ParseError> Serializer::deserialize(const std::string &jsonText) {
    Expected<std::string, ParseError> migrated = SchemaMigrator::migrate(jsonText);
    if (!migrated) {
        return Unexpected(migrated.error());
    }
    const json data = json::parse(*migrated, nullptr, false);
    if (data.is_discarded() || !data.is_object()) {
        return Unexpected(ParseError("failed to parse the document JSON"));
    }

    auto document = std::make_unique<Document>();

    Expected<EntityId, ParseError> id = IdField(data, "id");
    if (!id) {
        return Unexpected(id.error());
    }
    document->id = *id;

    Expected<std::string, ParseError> name = ParseField<std::string>(data, "name");
    if (!name) {
        return Unexpected(name.error());
    }
    document->name = std::move(*name);

    const json *assetsNode = FindChild(data, "assets");
    if (!assetsNode || !assetsNode->is_array()) {
        return Unexpected(ParseError("missing the assets array"));
    }
    for (const json &assetNode : *assetsNode) {
        Expected<Asset, ParseError> asset = AssetFromJson(assetNode);
        if (!asset) {
            return Unexpected(asset.error());
        }
        document->assets.push_back(std::move(*asset));
    }

    const json *compositionsNode = FindChild(data, "compositions");
    if (!compositionsNode || !compositionsNode->is_array()) {
        return Unexpected(ParseError("missing the compositions array"));
    }
    for (const json &compositionNode : *compositionsNode) {
        Expected<std::unique_ptr<Composition>, ParseError> composition =
            CompositionFromJson(compositionNode);
        if (!composition) {
            return Unexpected(composition.error());
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
