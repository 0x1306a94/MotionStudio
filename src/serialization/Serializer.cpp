#include "MotionStudio/serialization/Serializer.h"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/NullContent.h"
#include "MotionStudio/model/PrecompContent.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeEllipse.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
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

Expected<const json *, std::string> Child(const json &node, const char *key) {
    const json *child = FindChild(node, key);
    if (!child) {
        return Unexpected(std::string(std::string("missing field: ") + key));
    }
    return child;
}

Expected<float, std::string> AsFloat(const json &node) {
    if (!node.is_number()) {
        return Unexpected(std::string("field is not a number"));
    }
    return node.get<float>();
}

Expected<double, std::string> AsDouble(const json &node) {
    if (!node.is_number()) {
        return Unexpected(std::string("field is not a number"));
    }
    return node.get<double>();
}

Expected<int64_t, std::string> AsInt64(const json &node) {
    if (!node.is_number_integer()) {
        return Unexpected(std::string("field is not an integer"));
    }
    return node.get<int64_t>();
}

Expected<int, std::string> AsInt(const json &node) {
    Expected<int64_t, std::string> value = AsInt64(node);
    if (!value) {
        return Unexpected(value.error());
    }
    return static_cast<int>(*value);
}

Expected<uint32_t, std::string> AsUint32(const json &node) {
    Expected<int64_t, std::string> value = AsInt64(node);
    if (!value || *value < 0 || *value > static_cast<int64_t>(UINT32_MAX)) {
        return Unexpected(std::string("field is not a valid unsigned integer"));
    }
    return static_cast<uint32_t>(*value);
}

Expected<bool, std::string> AsBool(const json &node) {
    if (!node.is_boolean()) {
        return Unexpected(std::string("field is not a boolean"));
    }
    return node.get<bool>();
}

Expected<std::string, std::string> AsString(const json &node) {
    if (!node.is_string()) {
        return Unexpected(std::string("field is not a string"));
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

Expected<EntityId, std::string> IdFromString(const std::string &text) {
    uint64_t value = 0;
    const char *begin = text.data();
    const char *end = begin + text.size();
    const auto result = std::from_chars(begin, end, value, 16);
    if (result.ec != std::errc() || result.ptr != end || value == 0) {
        return Unexpected(std::string("invalid entity id: " + text));
    }
    return EntityId{value};
}

Expected<EntityId, std::string> IdField(const json &node, const char *key) {
    Expected<const json *, std::string> child = Child(node, key);
    if (!child) {
        return Unexpected(child.error());
    }
    Expected<std::string, std::string> text = AsString(**child);
    if (!text) {
        return Unexpected(text.error());
    }
    return IdFromString(*text);
}

// ---- Leaf values ----

json Vec2ToJson(Vec2 value) {
    return json::array({value.x, value.y});
}

Expected<Vec2, std::string> Vec2FromJson(const json &node) {
    if (!node.is_array() || node.size() != 2) {
        return Unexpected(std::string("Vec2 must be a 2-element array"));
    }
    Expected<float, std::string> x = AsFloat(node[0]);
    if (!x) {
        return Unexpected(x.error());
    }
    Expected<float, std::string> y = AsFloat(node[1]);
    if (!y) {
        return Unexpected(y.error());
    }
    return Vec2{*x, *y};
}

uint8_t ColorChannelToByte(float value) {
    const float clamped = std::fmin(std::fmax(value, 0.0f), 1.0f);
    return static_cast<uint8_t>(std::lroundf(clamped * 255.0f));
}

int HexDigitValue(char digit) {
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    if (digit >= 'a' && digit <= 'f') {
        return digit - 'a' + 10;
    }
    if (digit >= 'A' && digit <= 'F') {
        return digit - 'A' + 10;
    }
    return -1;
}

Expected<uint8_t, std::string> HexByteAt(const std::string &text, size_t offset) {
    const int high = HexDigitValue(text[offset]);
    const int low = HexDigitValue(text[offset + 1]);
    if (high < 0 || low < 0) {
        return Unexpected(std::string("Color hex string has a non-hex digit"));
    }
    return static_cast<uint8_t>(high * 16 + low);
}

json ColorToJson(Color value) {
    // #RRGGBBAA keeps the file readable and round-trip stable; colors are
    // quantized to 8 bits per channel.
    char text[10];
    std::snprintf(text, sizeof(text), "#%02X%02X%02X%02X",
                  ColorChannelToByte(value.r), ColorChannelToByte(value.g),
                  ColorChannelToByte(value.b), ColorChannelToByte(value.a));
    return std::string(text);
}

Expected<Color, std::string> ColorFromJson(const json &node) {
    if (node.is_string()) {
        const std::string text = node.get<std::string>();
        if (text.size() != 9 || text[0] != '#') {
            return Unexpected(std::string("Color must be a #RRGGBBAA hex string"));
        }
        uint8_t channels[4] = {};
        for (size_t index = 0; index < 4; ++index) {
            Expected<uint8_t, std::string> byte = HexByteAt(text, 1 + index * 2);
            if (!byte) {
                return Unexpected(byte.error());
            }
            channels[index] = *byte;
        }
        return Color{channels[0] / 255.0f, channels[1] / 255.0f,
                     channels[2] / 255.0f, channels[3] / 255.0f};
    }
    // Legacy format: 4-element float array [r, g, b, a].
    if (node.is_array() && node.size() == 4) {
        Expected<float, std::string> r = AsFloat(node[0]);
        Expected<float, std::string> g = AsFloat(node[1]);
        Expected<float, std::string> b = AsFloat(node[2]);
        Expected<float, std::string> a = AsFloat(node[3]);
        if (!r || !g || !b || !a) {
            return Unexpected(std::string("Color component is not a number"));
        }
        return Color{*r, *g, *b, *a};
    }
    return Unexpected(std::string("Color must be a #RRGGBBAA hex string"));
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

Expected<BezierPath, std::string> BezierPathFromJson(const json &node) {
    Expected<const json *, std::string> closedNode = Child(node, "closed");
    if (!closedNode) {
        return Unexpected(closedNode.error());
    }
    Expected<bool, std::string> closed = AsBool(**closedNode);
    if (!closed) {
        return Unexpected(closed.error());
    }
    const json *verticesNode = FindChild(node, "vertices");
    if (!verticesNode || !verticesNode->is_array()) {
        return Unexpected(std::string("missing the vertices array"));
    }
    BezierPath path;
    path.closed = *closed;
    for (const json &vertexNode : *verticesNode) {
        Expected<const json *, std::string> pointNode = Child(vertexNode, "point");
        Expected<const json *, std::string> inNode = Child(vertexNode, "inTangent");
        Expected<const json *, std::string> outNode = Child(vertexNode, "outTangent");
        if (!pointNode || !inNode || !outNode) {
            return Unexpected(std::string("vertex is missing a tangent field"));
        }
        Expected<Vec2, std::string> point = Vec2FromJson(**pointNode);
        Expected<Vec2, std::string> inTangent = Vec2FromJson(**inNode);
        Expected<Vec2, std::string> outTangent = Vec2FromJson(**outNode);
        if (!point || !inTangent || !outTangent) {
            return Unexpected(std::string("invalid vertex coordinates"));
        }
        path.vertices.push_back({*point, *inTangent, *outTangent});
    }
    return path;
}

template <typename T>
Expected<T, std::string> FromJson(const json &node);

template <>
Expected<float, std::string> FromJson<float>(const json &node) {
    return AsFloat(node);
}
template <>
Expected<double, std::string> FromJson<double>(const json &node) {
    return AsDouble(node);
}
template <>
Expected<int, std::string> FromJson<int>(const json &node) {
    return AsInt(node);
}
template <>
Expected<int64_t, std::string> FromJson<int64_t>(const json &node) {
    return AsInt64(node);
}
template <>
Expected<uint32_t, std::string> FromJson<uint32_t>(const json &node) {
    return AsUint32(node);
}
template <>
Expected<bool, std::string> FromJson<bool>(const json &node) {
    return AsBool(node);
}
template <>
Expected<std::string, std::string> FromJson<std::string>(const json &node) {
    return AsString(node);
}
template <>
Expected<Vec2, std::string> FromJson<Vec2>(const json &node) {
    return Vec2FromJson(node);
}
template <>
Expected<Color, std::string> FromJson<Color>(const json &node) {
    return ColorFromJson(node);
}
template <>
Expected<BezierPath, std::string> FromJson<BezierPath>(const json &node) {
    return BezierPathFromJson(node);
}

template <typename T>
Expected<T, std::string> ParseField(const json &node, const char *key) {
    Expected<const json *, std::string> child = Child(node, key);
    if (!child) {
        return Unexpected(child.error());
    }
    return FromJson<T>(**child);
}

// ---- Easing ----

std::string FormatFloat(float value) {
    char buffer[32];
    const int count = std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
    if (count <= 0) {
        return "0";
    }
    return std::string(buffer, static_cast<size_t>(count));
}

std::string CubicBezierToString(const Easing &easing) {
    return "cubic-bezier(" + FormatFloat(easing.inX) + "," + FormatFloat(easing.inY) +
        "," + FormatFloat(easing.outX) + "," + FormatFloat(easing.outY) + ")";
}

json EasingToJson(const Easing &easing) {
    switch (easing.type) {
        case EasingType::Linear: {
            return "linear";
        }
        case EasingType::Hold: {
            return "hold";
        }
        case EasingType::Ease: {
            return "ease";
        }
        case EasingType::EaseIn: {
            return "ease-in";
        }
        case EasingType::EaseOut: {
            return "ease-out";
        }
        case EasingType::EaseInOut: {
            return "ease-in-out";
        }
        case EasingType::CubicBezier: {
            return CubicBezierToString(easing);
        }
    }
    return "linear";
}

bool ConsumeChar(std::string_view text, size_t &position, char value) {
    while (position < text.size() && text[position] == ' ') {
        ++position;
    }
    if (position >= text.size() || text[position] != value) {
        return false;
    }
    ++position;
    return true;
}

Expected<float, std::string> ParseFloatToken(std::string_view text, size_t &position) {
    while (position < text.size() && text[position] == ' ') {
        ++position;
    }
    const size_t start = position;
    while (position < text.size() && text[position] != ',' && text[position] != ')') {
        ++position;
    }
    size_t end = position;
    while (end > start && text[end - 1] == ' ') {
        --end;
    }
    if (start == end) {
        return Unexpected(std::string("empty cubic-bezier control point"));
    }
    const std::string token{text.substr(start, end - start)};
    char *parseEnd = nullptr;
    errno = 0;
    const float value = std::strtof(token.c_str(), &parseEnd);
    if (errno != 0 || parseEnd == token.c_str() || *parseEnd != '\0' ||
        !std::isfinite(value)) {
        return Unexpected(std::string("invalid cubic-bezier control point"));
    }
    return value;
}

Expected<Easing, std::string> EasingFromString(const std::string &text) {
    if (text == "linear") {
        return Easing::Linear();
    }
    if (text == "hold") {
        return Easing::Hold();
    }
    if (text == "ease") {
        return Easing::Ease();
    }
    if (text == "ease-in") {
        return Easing::EaseIn();
    }
    if (text == "ease-out") {
        return Easing::EaseOut();
    }
    if (text == "ease-in-out") {
        return Easing::EaseInOut();
    }

    constexpr std::string_view prefix = "cubic-bezier(";
    std::string_view view{text};
    if (view.substr(0, prefix.size()) != prefix) {
        return Unexpected(std::string("unknown easing: " + text));
    }
    size_t position = prefix.size();
    Expected<float, std::string> inX = ParseFloatToken(view, position);
    if (!inX || !ConsumeChar(view, position, ',')) {
        return Unexpected(std::string("invalid cubic-bezier easing"));
    }
    Expected<float, std::string> inY = ParseFloatToken(view, position);
    if (!inY || !ConsumeChar(view, position, ',')) {
        return Unexpected(std::string("invalid cubic-bezier easing"));
    }
    Expected<float, std::string> outX = ParseFloatToken(view, position);
    if (!outX || !ConsumeChar(view, position, ',')) {
        return Unexpected(std::string("invalid cubic-bezier easing"));
    }
    Expected<float, std::string> outY = ParseFloatToken(view, position);
    if (!outY || !ConsumeChar(view, position, ')')) {
        return Unexpected(std::string("invalid cubic-bezier easing"));
    }
    while (position < view.size() && view[position] == ' ') {
        ++position;
    }
    if (position != view.size()) {
        return Unexpected(std::string("invalid cubic-bezier easing"));
    }
    return Easing::Bezier(*inX, *inY, *outX, *outY);
}

Expected<Easing, std::string> EasingFromJson(const json &node) {
    Expected<std::string, std::string> text = AsString(node);
    if (!text) {
        return Unexpected(std::string("Easing must be a string"));
    }
    return EasingFromString(*text);
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

json StaticFloatToJson(float value) {
    return json{{"static", value}};
}

json StaticVec2ToJson(Vec2 value) {
    return json{{"static", Vec2ToJson(value)}};
}

// Reads {"static": N}, bare number, or falls back when only legacy keyframes exist.
Expected<float, std::string> StaticFloatFromJson(const json &node, float fallback) {
    if (node.is_number()) {
        return static_cast<float>(node.get<double>());
    }
    if (!node.is_object()) {
        return Unexpected(std::string("Static float must be a number or object"));
    }
    if (const json *staticNode = FindChild(node, "static")) {
        return AsFloat(*staticNode);
    }
    return fallback;
}

Expected<Vec2, std::string> StaticVec2FromJson(const json &node, Vec2 fallback) {
    if (node.is_array()) {
        return Vec2FromJson(node);
    }
    if (!node.is_object()) {
        return Unexpected(std::string("Static Vec2 must be an array or object"));
    }
    if (const json *staticNode = FindChild(node, "static")) {
        return Vec2FromJson(*staticNode);
    }
    return fallback;
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
Expected<Keyframe<T>, std::string> KeyframeFromJson(const json &node) {
    Expected<int64_t, std::string> time = ParseField<int64_t>(node, "time");
    if (!time) {
        return Unexpected(time.error());
    }
    Expected<T, std::string> value = ParseField<T>(node, "value");
    if (!value) {
        return Unexpected(value.error());
    }
    Expected<const json *, std::string> easingNode = Child(node, "easing");
    if (!easingNode) {
        return Unexpected(easingNode.error());
    }
    Expected<Easing, std::string> easing = EasingFromJson(**easingNode);
    if (!easing) {
        return Unexpected(easing.error());
    }
    Keyframe<T> keyframe;
    keyframe.time = *time;
    keyframe.value = std::move(*value);
    keyframe.easing = *easing;
    if (const json *tangentNode = FindChild(node, "spatialInTangent")) {
        Expected<Vec2, std::string> tangent = Vec2FromJson(*tangentNode);
        if (!tangent) {
            return Unexpected(tangent.error());
        }
        keyframe.spatialInTangent = *tangent;
    }
    if (const json *tangentNode = FindChild(node, "spatialOutTangent")) {
        Expected<Vec2, std::string> tangent = Vec2FromJson(*tangentNode);
        if (!tangent) {
            return Unexpected(tangent.error());
        }
        keyframe.spatialOutTangent = *tangent;
    }
    return keyframe;
}

template <typename T>
Expected<void, std::string> AnimatableFromJson(const json &node, Animatable<T> &animatable) {
    if (const json *staticNode = FindChild(node, "static")) {
        Expected<T, std::string> value = FromJson<T>(*staticNode);
        if (!value) {
            return Unexpected(value.error());
        }
        animatable.setStaticValue(std::move(*value));
    }
    if (const json *keyframesNode = FindChild(node, "keyframes")) {
        if (!keyframesNode->is_array()) {
            return Unexpected(std::string("keyframes must be an array"));
        }
        for (const json &keyframeNode : *keyframesNode) {
            Expected<Keyframe<T>, std::string> keyframe = KeyframeFromJson<T>(keyframeNode);
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

Expected<void, std::string> TransformFromJson(const json &node, Transform &transform) {
    const char *fields[] = {"anchorPoint", "position", "scale", "rotation", "opacity"};
    Animatable<Vec2> *vec2Targets[] = {&transform.anchorPoint, &transform.position,
                                       &transform.scale};
    for (int i = 0; i < 3; ++i) {
        Expected<const json *, std::string> child = Child(node, fields[i]);
        if (!child) {
            return Unexpected(child.error());
        }
        Expected<void, std::string> result = AnimatableFromJson(**child, *vec2Targets[i]);
        if (!result) {
            return Unexpected(result.error());
        }
    }
    Expected<const json *, std::string> rotationNode = Child(node, fields[3]);
    if (!rotationNode) {
        return Unexpected(rotationNode.error());
    }
    Expected<void, std::string> result = AnimatableFromJson(**rotationNode, transform.rotation);
    if (!result) {
        return Unexpected(result.error());
    }
    Expected<const json *, std::string> opacityNode = Child(node, fields[4]);
    if (!opacityNode) {
        return Unexpected(opacityNode.error());
    }
    return AnimatableFromJson(**opacityNode, transform.opacity);
}

json MaskToJson(const Mask &mask) {
    return {{"path", AnimatableToJson(mask.path)},
            {"mode", dto::ToString(mask.mode)},
            {"opacity", AnimatableToJson(mask.opacity)},
            {"inverted", mask.inverted},
            {"feather", AnimatableToJson(mask.feather)},
            {"expansion", AnimatableToJson(mask.expansion)}};
}

Expected<Mask, std::string> MaskFromJson(const json &node) {
    Expected<std::string, std::string> modeText = ParseField<std::string>(node, "mode");
    if (!modeText) {
        return Unexpected(modeText.error());
    }
    Expected<MaskMode, std::string> mode = dto::maskModeFromString(*modeText);
    if (!mode) {
        return Unexpected(mode.error());
    }
    Mask mask;
    mask.mode = *mode;
    Expected<const json *, std::string> pathNode = Child(node, "path");
    if (!pathNode) {
        return Unexpected(pathNode.error());
    }
    // Current form is Animatable JSON; older docs stored a bare BezierPath.
    Expected<void, std::string> result;
    if (FindChild(**pathNode, "static") != nullptr ||
        FindChild(**pathNode, "keyframes") != nullptr) {
        result = AnimatableFromJson(**pathNode, mask.path);
        if (!result) {
            return Unexpected(result.error());
        }
    } else {
        Expected<BezierPath, std::string> path = BezierPathFromJson(**pathNode);
        if (!path) {
            return Unexpected(path.error());
        }
        mask.path.setStaticValue(std::move(*path));
    }
    Expected<const json *, std::string> opacityNode = Child(node, "opacity");
    if (!opacityNode) {
        return Unexpected(opacityNode.error());
    }
    result = AnimatableFromJson(**opacityNode, mask.opacity);
    if (!result) {
        return Unexpected(result.error());
    }
    Expected<bool, std::string> inverted = ParseField<bool>(node, "inverted");
    if (!inverted) {
        return Unexpected(inverted.error());
    }
    mask.inverted = *inverted;
    // feather/expansion are optional: documents saved before these fields
    // existed default to 0.
    if (const json *featherNode = FindChild(node, "feather")) {
        result = AnimatableFromJson(*featherNode, mask.feather);
        if (!result) {
            return Unexpected(result.error());
        }
    }
    if (const json *expansionNode = FindChild(node, "expansion")) {
        result = AnimatableFromJson(*expansionNode, mask.expansion);
        if (!result) {
            return Unexpected(result.error());
        }
    }
    return mask;
}

// ---- Shape elements (discriminant field "type") ----

json ShapeToJson(const ShapeElement &element) {
    json node{{"id", IdToString(element.id)}, {"type", dto::ToString(element.type())}};
    switch (element.type()) {
        case ShapeType::Path: {
            const auto &shape = static_cast<const ShapePath &>(element);
            node["path"] = AnimatableToJson(shape.path);
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

Expected<std::unique_ptr<ShapeElement>, std::string> ShapeFromJson(const json &node) {
    Expected<std::string, std::string> typeText = ParseField<std::string>(node, "type");
    if (!typeText) {
        return Unexpected(typeText.error());
    }
    Expected<ShapeType, std::string> type = dto::shapeTypeFromString(*typeText);
    if (!type) {
        return Unexpected(type.error());
    }
    std::unique_ptr<ShapeElement> element;
    switch (*type) {
        case ShapeType::Path: {
            auto shape = std::make_unique<ShapePath>();
            Expected<const json *, std::string> pathNode = Child(node, "path");
            if (!pathNode) {
                return Unexpected(pathNode.error());
            }
            Expected<void, std::string> result = AnimatableFromJson(**pathNode, shape->path);
            if (!result) {
                return Unexpected(result.error());
            }
            element = std::move(shape);
            break;
        }
        case ShapeType::Rect: {
            auto shape = std::make_unique<ShapeRect>();
            Expected<const json *, std::string> positionNode = Child(node, "position");
            if (!positionNode) {
                return Unexpected(positionNode.error());
            }
            Expected<void, std::string> result = AnimatableFromJson(**positionNode, shape->position);
            if (!result) {
                return Unexpected(result.error());
            }
            Expected<const json *, std::string> sizeNode = Child(node, "size");
            if (!sizeNode) {
                return Unexpected(sizeNode.error());
            }
            result = AnimatableFromJson(**sizeNode, shape->size);
            if (!result) {
                return Unexpected(result.error());
            }
            Expected<const json *, std::string> radiusNode = Child(node, "cornerRadius");
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
            Expected<const json *, std::string> positionNode = Child(node, "position");
            if (!positionNode) {
                return Unexpected(positionNode.error());
            }
            Expected<void, std::string> result = AnimatableFromJson(**positionNode, shape->position);
            if (!result) {
                return Unexpected(result.error());
            }
            Expected<const json *, std::string> sizeNode = Child(node, "size");
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
            Expected<const json *, std::string> startNode = Child(node, trimFields[0]);
            if (!startNode) {
                return Unexpected(startNode.error());
            }
            Expected<void, std::string> result = AnimatableFromJson(**startNode, shape->start);
            if (!result) {
                return Unexpected(result.error());
            }
            Expected<const json *, std::string> endNode = Child(node, trimFields[1]);
            if (!endNode) {
                return Unexpected(endNode.error());
            }
            result = AnimatableFromJson(**endNode, shape->end);
            if (!result) {
                return Unexpected(result.error());
            }
            Expected<const json *, std::string> offsetNode = Child(node, trimFields[2]);
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
    Expected<EntityId, std::string> id = IdField(node, "id");
    if (!id) {
        return Unexpected(id.error());
    }
    element->id = *id;
    return element;
}

// ---- LayerContent (discriminant field "type") ----

json LayerStyleToJson(const LayerStyle &style) {
    switch (style.type()) {
        case LayerStyleType::Fill: {
            const auto &fill = static_cast<const FillStyle &>(style);
            return {{"id", IdToString(fill.id)},
                    {"type", "fill"},
                    {"color", AnimatableToJson(fill.color)},
                    {"fillRule", dto::ToString(fill.fillRule)},
                    {"blendMode", dto::ToString(fill.blendMode)}};
        }
        case LayerStyleType::Stroke: {
            const auto &stroke = static_cast<const StrokeStyle &>(style);
            return {{"id", IdToString(stroke.id)},
                    {"type", "stroke"},
                    {"color", AnimatableToJson(stroke.color)},
                    {"width", AnimatableToJson(stroke.width)},
                    {"cap", dto::ToString(stroke.cap)},
                    {"join", dto::ToString(stroke.join)},
                    {"miterLimit", stroke.miterLimit},
                    {"blendMode", dto::ToString(stroke.blendMode)},
                    {"position", dto::ToString(stroke.position)},
                    {"trimStart", AnimatableToJson(stroke.trimStart)},
                    {"trimEnd", AnimatableToJson(stroke.trimEnd)},
                    {"trimOffset", AnimatableToJson(stroke.trimOffset)}};
        }
    }
    return json::object();
}

Expected<std::unique_ptr<LayerStyle>, std::string> LayerStyleFromJson(const json &node) {
    Expected<std::string, std::string> typeText = ParseField<std::string>(node, "type");
    if (!typeText) {
        return Unexpected(typeText.error());
    }

    std::unique_ptr<LayerStyle> style;
    if (*typeText == "fill") {
        auto fill = std::make_unique<FillStyle>();
        Expected<const json *, std::string> colorNode = Child(node, "color");
        if (!colorNode) {
            return Unexpected(std::string("Fill style is missing color field"));
        }
        Expected<void, std::string> result = AnimatableFromJson(**colorNode, fill->color);
        if (!result) {
            return Unexpected(result.error());
        }
        Expected<std::string, std::string> ruleText = ParseField<std::string>(node, "fillRule");
        if (!ruleText) {
            return Unexpected(ruleText.error());
        }
        Expected<FillRule, std::string> fillRule = dto::fillRuleFromString(*ruleText);
        if (!fillRule) {
            return Unexpected(fillRule.error());
        }
        fill->fillRule = *fillRule;
        // blendMode is optional: documents saved before fills had blend modes
        // default to Normal.
        Expected<std::string, std::string> blendText = ParseField<std::string>(node, "blendMode");
        if (blendText) {
            Expected<BlendMode, std::string> blendMode = dto::blendModeFromString(*blendText);
            if (!blendMode) {
                return Unexpected(blendMode.error());
            }
            fill->blendMode = *blendMode;
        }
        style = std::move(fill);
    } else if (*typeText == "stroke") {
        auto stroke = std::make_unique<StrokeStyle>();
        Expected<const json *, std::string> colorNode = Child(node, "color");
        Expected<const json *, std::string> widthNode = Child(node, "width");
        if (!colorNode || !widthNode) {
            return Unexpected(std::string("Stroke style is missing color/width fields"));
        }
        Expected<void, std::string> result = AnimatableFromJson(**colorNode, stroke->color);
        if (!result) {
            return Unexpected(result.error());
        }
        result = AnimatableFromJson(**widthNode, stroke->width);
        if (!result) {
            return Unexpected(result.error());
        }
        Expected<std::string, std::string> capText = ParseField<std::string>(node, "cap");
        Expected<std::string, std::string> joinText = ParseField<std::string>(node, "join");
        Expected<float, std::string> miterLimit = ParseField<float>(node, "miterLimit");
        if (!capText || !joinText || !miterLimit) {
            return Unexpected(std::string("Stroke style is missing cap/join/miterLimit fields"));
        }
        Expected<LineCap, std::string> cap = dto::lineCapFromString(*capText);
        Expected<LineJoin, std::string> join = dto::lineJoinFromString(*joinText);
        if (!cap || !join) {
            return Unexpected(std::string("Stroke style has invalid cap/join values"));
        }
        stroke->cap = *cap;
        stroke->join = *join;
        stroke->miterLimit = *miterLimit;
        // blendMode/position/trim are optional: documents saved before strokes
        // had them default to Normal/Center/full range.
        Expected<std::string, std::string> blendText = ParseField<std::string>(node, "blendMode");
        if (blendText) {
            Expected<BlendMode, std::string> blendMode = dto::blendModeFromString(*blendText);
            if (!blendMode) {
                return Unexpected(blendMode.error());
            }
            stroke->blendMode = *blendMode;
        }
        Expected<std::string, std::string> positionText =
            ParseField<std::string>(node, "position");
        if (positionText) {
            Expected<StrokePosition, std::string> position =
                dto::strokePositionFromString(*positionText);
            if (!position) {
                return Unexpected(position.error());
            }
            stroke->position = *position;
        }
        const std::pair<const char *, Animatable<float> *> trimFields[] = {
            {"trimStart", &stroke->trimStart},
            {"trimEnd", &stroke->trimEnd},
            {"trimOffset", &stroke->trimOffset},
        };
        for (const auto &[fieldName, target] : trimFields) {
            Expected<const json *, std::string> trimNode = Child(node, fieldName);
            if (!trimNode) {
                continue;
            }
            result = AnimatableFromJson(**trimNode, *target);
            if (!result) {
                return Unexpected(result.error());
            }
        }
        style = std::move(stroke);
    } else {
        return Unexpected(std::string("unknown layer style type: " + *typeText));
    }

    Expected<EntityId, std::string> id = IdField(node, "id");
    if (!id) {
        return Unexpected(id.error());
    }
    style->id = *id;
    return style;
}

json ContentToJson(const LayerContent &content) {
    json node{{"type", dto::ToString(content.type())}};
    switch (content.type()) {
        case LayerType::Shape: {
            const auto &shape = static_cast<const ShapeContent &>(content);
            if (shape.geometry) {
                node["geometry"] = ShapeToJson(*shape.geometry);
            } else {
                node["geometry"] = nullptr;
            }
            break;
        }
        case LayerType::Image: {
            const auto &image = static_cast<const ImageContent &>(content);
            // Optional ref: invalid → null (same as parentId). Never emit
            // "000…0" — IdFromString rejects zero as invalid.
            if (image.assetId.isValid()) {
                node["assetId"] = IdToString(image.assetId);
            } else {
                node["assetId"] = nullptr;
            }
            node["size"] = AnimatableToJson(image.size);
            node["scaleMode"] = dto::ToString(image.scaleMode);
            break;
        }
        case LayerType::Text: {
            const auto &text = static_cast<const TextContent &>(content);
            node["text"] = AnimatableToJson(text.text);
            node["fontFamily"] = text.fontFamily;
            node["fontStyle"] = text.fontStyle;
            node["fontSize"] = StaticFloatToJson(text.fontSize);
            node["size"] = StaticVec2ToJson(text.size);
            node["boxTextMode"] = text.boxTextMode;
            node["align"] = dto::ToString(text.align);
            break;
        }
        case LayerType::Group: {
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

Expected<std::unique_ptr<LayerContent>, std::string> ContentFromJson(const json &node) {
    Expected<std::string, std::string> typeText = ParseField<std::string>(node, "type");
    if (!typeText) {
        return Unexpected(typeText.error());
    }
    Expected<LayerType, std::string> type = dto::layerTypeFromString(*typeText);
    if (!type) {
        return Unexpected(type.error());
    }
    switch (*type) {
        case LayerType::Shape: {
            auto content = std::make_unique<ShapeContent>();
            Expected<const json *, std::string> geometryNode = Child(node, "geometry");
            if (!geometryNode) {
                return Unexpected(geometryNode.error());
            }
            if (!(**geometryNode).is_null()) {
                Expected<std::unique_ptr<ShapeElement>, std::string> geometry =
                    ShapeFromJson(**geometryNode);
                if (!geometry) {
                    return Unexpected(geometry.error());
                }
                content->geometry = std::move(*geometry);
            }
            return std::unique_ptr<LayerContent>(std::move(content));
        }
        case LayerType::Image: {
            auto content = std::make_unique<ImageContent>();
            if (const json *assetIdNode = FindChild(node, "assetId")) {
                if (!assetIdNode->is_null()) {
                    Expected<std::string, std::string> assetIdText = AsString(*assetIdNode);
                    if (!assetIdText) {
                        return Unexpected(assetIdText.error());
                    }
                    Expected<EntityId, std::string> assetId = IdFromString(*assetIdText);
                    if (!assetId) {
                        return Unexpected(assetId.error());
                    }
                    content->assetId = *assetId;
                }
            }
            if (const json *sizeNode = FindChild(node, "size")) {
                Expected<void, std::string> result = AnimatableFromJson(*sizeNode, content->size);
                if (!result) {
                    return Unexpected(result.error());
                }
            }
            if (const json *scaleModeNode = FindChild(node, "scaleMode")) {
                if (!scaleModeNode->is_string()) {
                    return Unexpected(std::string("Image scaleMode must be a string"));
                }
                Expected<ImageScaleMode, std::string> mode =
                    dto::imageScaleModeFromString(scaleModeNode->get<std::string>());
                if (!mode) {
                    return Unexpected(mode.error());
                }
                content->scaleMode = *mode;
            }
            return std::unique_ptr<LayerContent>(std::move(content));
        }
        case LayerType::Text: {
            auto content = std::make_unique<TextContent>();
            Expected<const json *, std::string> textNode = Child(node, "text");
            if (!textNode) {
                return Unexpected(textNode.error());
            }
            Expected<void, std::string> result = AnimatableFromJson(**textNode, content->text);
            if (!result) {
                return Unexpected(result.error());
            }
            Expected<std::string, std::string> fontFamily = ParseField<std::string>(node, "fontFamily");
            if (!fontFamily) {
                return Unexpected(fontFamily.error());
            }
            content->fontFamily = std::move(*fontFamily);
            if (const json *fontStyleNode = FindChild(node, "fontStyle")) {
                Expected<std::string, std::string> fontStyle = AsString(*fontStyleNode);
                if (!fontStyle) {
                    return Unexpected(fontStyle.error());
                }
                content->fontStyle = std::move(*fontStyle);
            }
            Expected<const json *, std::string> fontSizeNode = Child(node, "fontSize");
            if (!fontSizeNode) {
                return Unexpected(fontSizeNode.error());
            }
            Expected<float, std::string> fontSize =
                StaticFloatFromJson(**fontSizeNode, content->fontSize);
            if (!fontSize) {
                return Unexpected(fontSize.error());
            }
            content->fontSize = *fontSize;
            if (const json *sizeNode = FindChild(node, "size")) {
                Expected<Vec2, std::string> size = StaticVec2FromJson(*sizeNode, content->size);
                if (!size) {
                    return Unexpected(size.error());
                }
                content->size = *size;
            }
            if (const json *boxTextModeNode = FindChild(node, "boxTextMode")) {
                Expected<bool, std::string> boxTextMode = AsBool(*boxTextModeNode);
                if (!boxTextMode) {
                    return Unexpected(boxTextMode.error());
                }
                content->boxTextMode = *boxTextMode;
            }
            if (const json *alignNode = FindChild(node, "align")) {
                if (!alignNode->is_string()) {
                    return Unexpected(std::string("Text align must be a string"));
                }
                Expected<TextAlign, std::string> align =
                    dto::textAlignFromString(alignNode->get<std::string>());
                if (!align) {
                    return Unexpected(align.error());
                }
                content->align = *align;
            }
            return std::unique_ptr<LayerContent>(std::move(content));
        }
        case LayerType::Group: {
            return std::unique_ptr<LayerContent>(std::make_unique<NullContent>());
        }
        case LayerType::Precomp: {
            auto content = std::make_unique<PrecompContent>();
            Expected<EntityId, std::string> compositionId = IdField(node, "compositionId");
            if (!compositionId) {
                return Unexpected(compositionId.error());
            }
            content->compositionId = *compositionId;
            return std::unique_ptr<LayerContent>(std::move(content));
        }
    }
    return Unexpected(std::string("unknown layer content type"));
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
    node["trackMatteType"] = dto::ToString(layer.trackMatteType);
    if (layer.trackMatteLayerId.isValid()) {
        node["trackMatteLayerId"] = IdToString(layer.trackMatteLayerId);
    } else {
        node["trackMatteLayerId"] = nullptr;
    }
    json followPath = json{{"enabled", layer.followPath.enabled},
                           {"pathOffset", AnimatableToJson(layer.followPath.pathOffset)},
                           {"orientAlongPath", layer.followPath.orientAlongPath},
                           {"orientOffset", AnimatableToJson(layer.followPath.orientOffset)}};
    if (layer.followPath.pathLayerId.isValid()) {
        followPath["pathLayerId"] = IdToString(layer.followPath.pathLayerId);
    } else {
        followPath["pathLayerId"] = nullptr;
    }
    node["followPath"] = std::move(followPath);
    json styles = json::array();
    for (const auto &style : layer.styles) {
        styles.push_back(LayerStyleToJson(*style));
    }
    node["styles"] = std::move(styles);
    return node;
}

Expected<std::unique_ptr<Layer>, std::string> LayerFromJson(const json &node) {
    Expected<std::string, std::string> typeText = ParseField<std::string>(node, "type");
    if (!typeText) {
        return Unexpected(typeText.error());
    }
    Expected<LayerType, std::string> type = dto::layerTypeFromString(*typeText);
    if (!type) {
        return Unexpected(type.error());
    }
    auto layer = std::make_unique<Layer>(*type);

    Expected<EntityId, std::string> id = IdField(node, "id");
    if (!id) {
        return Unexpected(id.error());
    }
    layer->id = *id;

    Expected<std::string, std::string> name = ParseField<std::string>(node, "name");
    if (!name) {
        return Unexpected(name.error());
    }
    layer->name = std::move(*name);

    Expected<int64_t, std::string> inPoint = ParseField<int64_t>(node, "inPoint");
    Expected<int64_t, std::string> outPoint = ParseField<int64_t>(node, "outPoint");
    Expected<int64_t, std::string> startTime = ParseField<int64_t>(node, "startTime");
    Expected<double, std::string> timeStretch = ParseField<double>(node, "timeStretch");
    Expected<bool, std::string> visible = ParseField<bool>(node, "visible");
    Expected<bool, std::string> locked = ParseField<bool>(node, "locked");
    if (!inPoint || !outPoint || !startTime || !timeStretch || !visible || !locked) {
        return Unexpected(std::string("Layer is missing the time/visibility fields"));
    }
    layer->inPoint = *inPoint;
    layer->outPoint = *outPoint;
    layer->startTime = *startTime;
    layer->timeStretch = *timeStretch;
    layer->visible = *visible;
    layer->locked = *locked;

    Expected<std::string, std::string> blendText = ParseField<std::string>(node, "blendMode");
    if (!blendText) {
        return Unexpected(blendText.error());
    }
    Expected<BlendMode, std::string> blendMode = dto::blendModeFromString(*blendText);
    if (!blendMode) {
        return Unexpected(blendMode.error());
    }
    layer->blendMode = *blendMode;

    Expected<const json *, std::string> transformNode = Child(node, "transform");
    if (!transformNode) {
        return Unexpected(transformNode.error());
    }
    Expected<void, std::string> result = TransformFromJson(**transformNode, layer->transform);
    if (!result) {
        return Unexpected(result.error());
    }

    Expected<const json *, std::string> contentNode = Child(node, "content");
    if (!contentNode) {
        return Unexpected(contentNode.error());
    }
    Expected<std::unique_ptr<LayerContent>, std::string> content = ContentFromJson(**contentNode);
    if (!content) {
        return Unexpected(content.error());
    }
    layer->content = std::move(*content);

    const json *parentNode = FindChild(node, "parentId");
    if (parentNode && !parentNode->is_null()) {
        Expected<std::string, std::string> parentText = AsString(*parentNode);
        if (!parentText) {
            return Unexpected(parentText.error());
        }
        Expected<EntityId, std::string> parentId = IdFromString(*parentText);
        if (!parentId) {
            return Unexpected(parentId.error());
        }
        layer->parentId = *parentId;
    }

    if (const json *masksNode = FindChild(node, "masks")) {
        if (!masksNode->is_array()) {
            return Unexpected(std::string("masks must be an array"));
        }
        for (const json &maskNode : *masksNode) {
            Expected<Mask, std::string> mask = MaskFromJson(maskNode);
            if (!mask) {
                return Unexpected(mask.error());
            }
            layer->masks.push_back(std::move(*mask));
        }
    }

    // trackMatteType is optional: documents saved before track matte existed
    // default to None / invalid source id.
    Expected<std::string, std::string> trackMatteTypeText = ParseField<std::string>(node, "trackMatteType");
    if (trackMatteTypeText) {
        Expected<TrackMatteType, std::string> trackMatteType = dto::trackMatteTypeFromString(*trackMatteTypeText);
        if (!trackMatteType) {
            return Unexpected(trackMatteType.error());
        }
        layer->trackMatteType = *trackMatteType;
    }
    const json *trackMatteLayerNode = FindChild(node, "trackMatteLayerId");
    if (trackMatteLayerNode && !trackMatteLayerNode->is_null()) {
        Expected<std::string, std::string> matteIdText = AsString(*trackMatteLayerNode);
        if (!matteIdText) {
            return Unexpected(matteIdText.error());
        }
        Expected<EntityId, std::string> matteLayerId = IdFromString(*matteIdText);
        if (!matteLayerId) {
            return Unexpected(matteLayerId.error());
        }
        layer->trackMatteLayerId = *matteLayerId;
    }

    if (const json *followPathNode = FindChild(node, "followPath")) {
        if (!followPathNode->is_object()) {
            return Unexpected(std::string("followPath must be an object"));
        }
        Expected<bool, std::string> enabled = ParseField<bool>(*followPathNode, "enabled");
        if (enabled) {
            layer->followPath.enabled = *enabled;
        }
        Expected<bool, std::string> orientAlongPath =
            ParseField<bool>(*followPathNode, "orientAlongPath");
        if (orientAlongPath) {
            layer->followPath.orientAlongPath = *orientAlongPath;
        }
        const json *pathLayerNode = FindChild(*followPathNode, "pathLayerId");
        if (pathLayerNode && !pathLayerNode->is_null()) {
            Expected<std::string, std::string> pathLayerText = AsString(*pathLayerNode);
            if (!pathLayerText) {
                return Unexpected(pathLayerText.error());
            }
            Expected<EntityId, std::string> pathLayerId = IdFromString(*pathLayerText);
            if (!pathLayerId) {
                return Unexpected(pathLayerId.error());
            }
            layer->followPath.pathLayerId = *pathLayerId;
        }
        if (const json *pathOffsetNode = FindChild(*followPathNode, "pathOffset")) {
            Expected<void, std::string> pathOffsetResult =
                AnimatableFromJson(*pathOffsetNode, layer->followPath.pathOffset);
            if (!pathOffsetResult) {
                return Unexpected(pathOffsetResult.error());
            }
        }
        if (const json *orientOffsetNode = FindChild(*followPathNode, "orientOffset")) {
            Expected<void, std::string> orientOffsetResult =
                AnimatableFromJson(*orientOffsetNode, layer->followPath.orientOffset);
            if (!orientOffsetResult) {
                return Unexpected(orientOffsetResult.error());
            }
        }
    }

    Expected<const json *, std::string> stylesNode = Child(node, "styles");
    if (!stylesNode) {
        return Unexpected(stylesNode.error());
    }
    if (!(**stylesNode).is_array()) {
        return Unexpected(std::string("styles must be an array"));
    }
    for (const json &styleNode : **stylesNode) {
        Expected<std::unique_ptr<LayerStyle>, std::string> style = LayerStyleFromJson(styleNode);
        if (!style) {
            return Unexpected(style.error());
        }
        layer->styles.push_back(std::move(*style));
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
            {"cornerRadius", composition.cornerRadius},
            {"layers", std::move(layers)}};
}

Expected<std::unique_ptr<Composition>, std::string> CompositionFromJson(const json &node) {
    auto composition = std::make_unique<Composition>();

    Expected<EntityId, std::string> id = IdField(node, "id");
    if (!id) {
        return Unexpected(id.error());
    }
    composition->id = *id;

    Expected<std::string, std::string> name = ParseField<std::string>(node, "name");
    if (!name) {
        return Unexpected(name.error());
    }
    composition->name = std::move(*name);

    Expected<int64_t, std::string> duration = ParseField<int64_t>(node, "duration");
    if (!duration) {
        return Unexpected(duration.error());
    }
    composition->duration = *duration;

    Expected<const json *, std::string> frameRateNode = Child(node, "frameRate");
    if (!frameRateNode) {
        return Unexpected(frameRateNode.error());
    }
    Expected<uint32_t, std::string> num = ParseField<uint32_t>(**frameRateNode, "num");
    Expected<uint32_t, std::string> den = ParseField<uint32_t>(**frameRateNode, "den");
    if (!num || !den) {
        return Unexpected(std::string("frameRate is missing the num/den fields"));
    }
    composition->frameRate = {*num, *den};

    Expected<int, std::string> width = ParseField<int>(node, "width");
    Expected<int, std::string> height = ParseField<int>(node, "height");
    if (!width || !height) {
        return Unexpected(std::string("Composition is missing the width/height fields"));
    }
    composition->width = *width;
    composition->height = *height;

    Expected<Color, std::string> backgroundColor = ParseField<Color>(node, "backgroundColor");
    if (!backgroundColor) {
        return Unexpected(backgroundColor.error());
    }
    composition->backgroundColor = *backgroundColor;

    if (const json *cornerRadiusNode = FindChild(node, "cornerRadius")) {
        Expected<float, std::string> cornerRadius = AsFloat(*cornerRadiusNode);
        if (!cornerRadius) {
            return Unexpected(cornerRadius.error());
        }
        composition->cornerRadius = *cornerRadius;
    }

    const json *layersNode = FindChild(node, "layers");
    if (!layersNode || !layersNode->is_array()) {
        return Unexpected(std::string("Composition is missing the layers array"));
    }
    for (const json &layerNode : *layersNode) {
        Expected<std::unique_ptr<Layer>, std::string> layer = LayerFromJson(layerNode);
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
            {"path", asset.path},
            {"width", asset.width},
            {"height", asset.height}};
}

Expected<Asset, std::string> AssetFromJson(const json &node) {
    Asset asset;
    Expected<EntityId, std::string> id = IdField(node, "id");
    if (!id) {
        return Unexpected(id.error());
    }
    asset.id = *id;
    Expected<std::string, std::string> typeText = ParseField<std::string>(node, "type");
    if (!typeText) {
        return Unexpected(typeText.error());
    }
    Expected<AssetType, std::string> type = dto::assetTypeFromString(*typeText);
    if (!type) {
        return Unexpected(type.error());
    }
    asset.type = *type;
    Expected<std::string, std::string> name = ParseField<std::string>(node, "name");
    Expected<std::string, std::string> path = ParseField<std::string>(node, "path");
    if (!name || !path) {
        return Unexpected(std::string("Asset is missing the name/path fields"));
    }
    asset.name = std::move(*name);
    asset.path = std::move(*path);
    if (const json *widthNode = FindChild(node, "width")) {
        if (!widthNode->is_number_integer()) {
            return Unexpected(std::string("Asset width must be an integer"));
        }
        asset.width = widthNode->get<int>();
    }
    if (const json *heightNode = FindChild(node, "height")) {
        if (!heightNode->is_number_integer()) {
            return Unexpected(std::string("Asset height must be an integer"));
        }
        asset.height = heightNode->get<int>();
    }
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

Expected<std::unique_ptr<Document>, std::string> Serializer::deserialize(const std::string &jsonText) {
    Expected<std::string, std::string> migrated = SchemaMigrator::migrate(jsonText);
    if (!migrated) {
        return Unexpected(migrated.error());
    }
    const json data = json::parse(*migrated, nullptr, false);
    if (data.is_discarded() || !data.is_object()) {
        return Unexpected(std::string("failed to parse the document JSON"));
    }

    auto document = std::make_unique<Document>();

    Expected<EntityId, std::string> id = IdField(data, "id");
    if (!id) {
        return Unexpected(id.error());
    }
    document->id = *id;

    Expected<std::string, std::string> name = ParseField<std::string>(data, "name");
    if (!name) {
        return Unexpected(name.error());
    }
    document->name = std::move(*name);

    const json *assetsNode = FindChild(data, "assets");
    if (!assetsNode || !assetsNode->is_array()) {
        return Unexpected(std::string("missing the assets array"));
    }
    for (const json &assetNode : *assetsNode) {
        // Development-era Font assets are dropped on load; re-save omits them.
        if (const json *typeNode = FindChild(assetNode, "type")) {
            if (typeNode->is_string() && typeNode->get<std::string>() == "font") {
                continue;
            }
        }
        Expected<Asset, std::string> asset = AssetFromJson(assetNode);
        if (!asset) {
            return Unexpected(asset.error());
        }
        document->assets.push_back(std::move(*asset));
    }

    const json *compositionsNode = FindChild(data, "compositions");
    if (!compositionsNode || !compositionsNode->is_array()) {
        return Unexpected(std::string("missing the compositions array"));
    }
    for (const json &compositionNode : *compositionsNode) {
        Expected<std::unique_ptr<Composition>, std::string> composition =
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
