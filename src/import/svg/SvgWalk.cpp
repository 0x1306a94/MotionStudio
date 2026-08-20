#include "SvgWalk.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>

#include "MotionStudio/model/ImageContent.h"
#include "MotionStudio/model/ImageScaleMode.h"
#include "MotionStudio/model/NullContent.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapePath.h"
#include "MotionStudio/model/ShapeRect.h"
#include "MotionStudio/model/TrackMatteType.h"
#include "SvgLength.h"
#include "SvgPathConvert.h"
#include "SvgStyle.h"
#include "SvgText.h"
#include "SvgTransform.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Size.h"
#include "tgfx/svg/node/SVGCircle.h"
#include "tgfx/svg/node/SVGClipPath.h"
#include "tgfx/svg/node/SVGContainer.h"
#include "tgfx/svg/node/SVGEllipse.h"
#include "tgfx/svg/node/SVGImage.h"
#include "tgfx/svg/node/SVGLine.h"
#include "tgfx/svg/node/SVGMask.h"
#include "tgfx/svg/node/SVGNode.h"
#include "tgfx/svg/node/SVGPath.h"
#include "tgfx/svg/node/SVGPattern.h"
#include "tgfx/svg/node/SVGPoly.h"
#include "tgfx/svg/node/SVGRect.h"
#include "tgfx/svg/node/SVGTransformableNode.h"
#include "tgfx/svg/node/SVGUse.h"

namespace motion {
namespace svg {

namespace {

bool IsSkippedContainer(tgfx::SVGTag tag) {
    switch (tag) {
        case tgfx::SVGTag::Defs:
        case tgfx::SVGTag::LinearGradient:
        case tgfx::SVGTag::RadialGradient:
        case tgfx::SVGTag::ClipPath:
        case tgfx::SVGTag::Mask:
        case tgfx::SVGTag::Filter:
        case tgfx::SVGTag::Pattern:
        case tgfx::SVGTag::Stop:
            return true;
        default:
            return false;
    }
}

bool IsTextNode(tgfx::SVGTag tag) {
    switch (tag) {
        case tgfx::SVGTag::Text:
        case tgfx::SVGTag::TextPath:
        case tgfx::SVGTag::TSpan:
        case tgfx::SVGTag::TextLiteral:
            return true;
        default:
            return false;
    }
}

bool IsFilterPrimitive(tgfx::SVGTag tag) {
    switch (tag) {
        case tgfx::SVGTag::FeBlend:
        case tgfx::SVGTag::FeColorMatrix:
        case tgfx::SVGTag::FeComponentTransfer:
        case tgfx::SVGTag::FeComposite:
        case tgfx::SVGTag::FeDiffuseLighting:
        case tgfx::SVGTag::FeDisplacementMap:
        case tgfx::SVGTag::FeDistantLight:
        case tgfx::SVGTag::FeFlood:
        case tgfx::SVGTag::FeFuncA:
        case tgfx::SVGTag::FeFuncR:
        case tgfx::SVGTag::FeFuncG:
        case tgfx::SVGTag::FeFuncB:
        case tgfx::SVGTag::FeGaussianBlur:
        case tgfx::SVGTag::FeImage:
        case tgfx::SVGTag::FeMerge:
        case tgfx::SVGTag::FeMergeNode:
        case tgfx::SVGTag::FeMorphology:
        case tgfx::SVGTag::FeOffset:
        case tgfx::SVGTag::FePointLight:
        case tgfx::SVGTag::FeSpecularLighting:
        case tgfx::SVGTag::FeSpotLight:
        case tgfx::SVGTag::FeTurbulence:
            return true;
        default:
            return false;
    }
}

std::string FirstToken(const std::string &value) {
    size_t start = 0;
    while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) {
        start += 1;
    }
    size_t end = start;
    while (end < value.size() && value[end] != ' ' && value[end] != '\t') {
        end += 1;
    }
    return value.substr(start, end - start);
}

std::string DefaultName(tgfx::SVGTag tag) {
    switch (tag) {
        case tgfx::SVGTag::Path:
            return "Path";
        case tgfx::SVGTag::Rect:
            return "Rectangle";
        case tgfx::SVGTag::Circle:
        case tgfx::SVGTag::Ellipse:
            return "Ellipse";
        case tgfx::SVGTag::Line:
            return "Line";
        case tgfx::SVGTag::Polygon:
        case tgfx::SVGTag::Polyline:
            return "Polygon";
        case tgfx::SVGTag::G:
        case tgfx::SVGTag::Svg:
        case tgfx::SVGTag::Use:
            return "Group";
        case tgfx::SVGTag::Image:
            return "Image";
        case tgfx::SVGTag::Text:
            return "Text";
        default:
            return "Path";
    }
}

std::string LayerName(const tgfx::SVGNode &node) {
    const auto &id = node.getID();
    if (id.isValue() && !id->empty()) {
        return *id;
    }
    const auto &className = node.getClass();
    if (className.isValue()) {
        const std::string token = FirstToken(*className);
        if (!token.empty()) {
            return token;
        }
    }
    return DefaultName(node.tag());
}

struct SvgRectGeom {
    float x = 0.f;
    float y = 0.f;
    float width = 0.f;
    float height = 0.f;
    float rx = 0.f;
    float ry = 0.f;
};

constexpr float UniformRadiusEpsilon = 1e-3f;

bool ResolveSvgRectGeom(const tgfx::SVGNode &node, const tgfx::SVGLengthContext &lengthContext,
                        SvgRectGeom *out) {
    if (out == nullptr || node.tag() != tgfx::SVGTag::Rect) {
        return false;
    }
    const auto &rect = static_cast<const tgfx::SVGRect &>(node);
    const float x =
        lengthContext.resolve(rect.getX(), tgfx::SVGLengthContext::LengthType::Horizontal);
    const float y =
        lengthContext.resolve(rect.getY(), tgfx::SVGLengthContext::LengthType::Vertical);
    const float width =
        lengthContext.resolve(rect.getWidth(), tgfx::SVGLengthContext::LengthType::Horizontal);
    const float height =
        lengthContext.resolve(rect.getHeight(), tgfx::SVGLengthContext::LengthType::Vertical);
    if (width <= 0.f || height <= 0.f) {
        return false;
    }
    float rx = 0.f;
    float ry = 0.f;
    if (rect.getRx().has_value()) {
        rx = lengthContext.resolve(*rect.getRx(), tgfx::SVGLengthContext::LengthType::Horizontal);
    }
    if (rect.getRy().has_value()) {
        ry = lengthContext.resolve(*rect.getRy(), tgfx::SVGLengthContext::LengthType::Vertical);
    }
    if (rx <= 0.f && ry > 0.f) {
        rx = ry;
    }
    if (ry <= 0.f && rx > 0.f) {
        ry = rx;
    }
    rx = std::min(rx, width * 0.5f);
    ry = std::min(ry, height * 0.5f);
    out->x = x;
    out->y = y;
    out->width = width;
    out->height = height;
    out->rx = rx;
    out->ry = ry;
    return true;
}

bool SvgRectHasUniformRadius(const SvgRectGeom &geom) {
    return std::fabs(geom.rx - geom.ry) <= UniformRadiusEpsilon;
}

bool MappedRectMatchesImageBox(Vec2 p00, Vec2 p10, Vec2 p01, Vec2 p11, float rxLength,
                               float ryLength, float imageWidth, float imageHeight,
                               float *imageRadius) {
    const float boxEpsilon = std::max(0.5f, 0.01f * std::min(imageWidth, imageHeight));
    if (std::fabs(p10.y - p00.y) > boxEpsilon || std::fabs(p01.x - p00.x) > boxEpsilon) {
        return false;
    }
    const float minX = std::min(std::min(p00.x, p10.x), std::min(p01.x, p11.x));
    const float maxX = std::max(std::max(p00.x, p10.x), std::max(p01.x, p11.x));
    const float minY = std::min(std::min(p00.y, p10.y), std::min(p01.y, p11.y));
    const float maxY = std::max(std::max(p00.y, p10.y), std::max(p01.y, p11.y));
    if (std::fabs(minX) > boxEpsilon || std::fabs(minY) > boxEpsilon) {
        return false;
    }
    if (std::fabs(maxX - imageWidth) > boxEpsilon || std::fabs(maxY - imageHeight) > boxEpsilon) {
        return false;
    }
    const float radiusEpsilon = std::max(UniformRadiusEpsilon, 0.01f * std::max(rxLength, ryLength));
    if (std::fabs(rxLength - ryLength) > radiusEpsilon) {
        return false;
    }
    if (imageRadius != nullptr) {
        *imageRadius = 0.5f * (rxLength + ryLength);
    }
    return true;
}

bool NetworkHasArea(const VectorNetwork &network) {
    if (network.vertices.size() < 2) {
        return false;
    }
    float minX = network.vertices.front().point.x;
    float minY = network.vertices.front().point.y;
    float maxX = minX;
    float maxY = minY;
    for (const VectorNetwork::Vertex &vertex : network.vertices) {
        minX = std::min(minX, vertex.point.x);
        minY = std::min(minY, vertex.point.y);
        maxX = std::max(maxX, vertex.point.x);
        maxY = std::max(maxY, vertex.point.y);
    }
    return (maxX - minX) > 1e-6f || (maxY - minY) > 1e-6f;
}

tgfx::Path ShapePathFromNode(const tgfx::SVGNode &node,
                             const tgfx::SVGLengthContext &lengthContext) {
    tgfx::Path path;
    switch (node.tag()) {
        case tgfx::SVGTag::Path: {
            const auto &svgPath = static_cast<const tgfx::SVGPath &>(node);
            path = svgPath.getShapePath();
            break;
        }
        case tgfx::SVGTag::Rect: {
            SvgRectGeom geom = {};
            if (!ResolveSvgRectGeom(node, lengthContext, &geom)) {
                break;
            }
            const tgfx::Rect bounds =
                tgfx::Rect::MakeXYWH(geom.x, geom.y, geom.width, geom.height);
            if (geom.rx > 0.f || geom.ry > 0.f) {
                path.addRoundRect(bounds, geom.rx, geom.ry);
            } else {
                path.addRect(bounds);
            }
            break;
        }
        case tgfx::SVGTag::Circle: {
            const auto &circle = static_cast<const tgfx::SVGCircle &>(node);
            const float cx = lengthContext.resolve(circle.getCx(),
                                                   tgfx::SVGLengthContext::LengthType::Horizontal);
            const float cy = lengthContext.resolve(circle.getCy(),
                                                   tgfx::SVGLengthContext::LengthType::Vertical);
            const float radius =
                lengthContext.resolve(circle.getR(), tgfx::SVGLengthContext::LengthType::Other);
            if (radius > 0.f) {
                path.addOval(tgfx::Rect::MakeXYWH(cx - radius, cy - radius, radius * 2.f,
                                                  radius * 2.f));
            }
            break;
        }
        case tgfx::SVGTag::Ellipse: {
            const auto &ellipse = static_cast<const tgfx::SVGEllipse &>(node);
            const float cx = lengthContext.resolve(ellipse.getCx(),
                                                   tgfx::SVGLengthContext::LengthType::Horizontal);
            const float cy = lengthContext.resolve(ellipse.getCy(),
                                                   tgfx::SVGLengthContext::LengthType::Vertical);
            float rx = 0.f;
            float ry = 0.f;
            if (ellipse.getRx().has_value()) {
                rx = lengthContext.resolve(*ellipse.getRx(),
                                           tgfx::SVGLengthContext::LengthType::Horizontal);
            }
            if (ellipse.getRy().has_value()) {
                ry = lengthContext.resolve(*ellipse.getRy(),
                                           tgfx::SVGLengthContext::LengthType::Vertical);
            }
            if (rx <= 0.f && ry > 0.f) {
                rx = ry;
            }
            if (ry <= 0.f && rx > 0.f) {
                ry = rx;
            }
            if (rx > 0.f && ry > 0.f) {
                path.addOval(tgfx::Rect::MakeXYWH(cx - rx, cy - ry, rx * 2.f, ry * 2.f));
            }
            break;
        }
        case tgfx::SVGTag::Line: {
            const auto &line = static_cast<const tgfx::SVGLine &>(node);
            const float x1 =
                lengthContext.resolve(line.getX1(), tgfx::SVGLengthContext::LengthType::Horizontal);
            const float y1 =
                lengthContext.resolve(line.getY1(), tgfx::SVGLengthContext::LengthType::Vertical);
            const float x2 =
                lengthContext.resolve(line.getX2(), tgfx::SVGLengthContext::LengthType::Horizontal);
            const float y2 =
                lengthContext.resolve(line.getY2(), tgfx::SVGLengthContext::LengthType::Vertical);
            path.moveTo(x1, y1);
            path.lineTo(x2, y2);
            break;
        }
        case tgfx::SVGTag::Polygon:
        case tgfx::SVGTag::Polyline: {
            const auto &poly = static_cast<const tgfx::SVGPoly &>(node);
            const tgfx::SVGPointsType &points = poly.getPoints();
            if (points.empty()) {
                break;
            }
            path.moveTo(points.front());
            for (size_t i = 1; i < points.size(); ++i) {
                path.lineTo(points[i]);
            }
            if (node.tag() == tgfx::SVGTag::Polygon) {
                path.close();
            }
            break;
        }
        default:
            break;
    }
    return path;
}

void ApplyNodeOpacity(Layer &layer, const tgfx::SVGNode &node) {
    const auto &opacity = node.getOpacity();
    if (opacity.isValue()) {
        layer.transform.opacity.setStaticValue(*opacity);
    }
}

void ApplyNodeTransform(Layer &layer, const tgfx::SVGNode &node) {
    const tgfx::SVGTag tag = node.tag();
    switch (tag) {
        case tgfx::SVGTag::Path:
        case tgfx::SVGTag::Rect:
        case tgfx::SVGTag::Circle:
        case tgfx::SVGTag::Ellipse:
        case tgfx::SVGTag::Line:
        case tgfx::SVGTag::Polygon:
        case tgfx::SVGTag::Polyline:
        case tgfx::SVGTag::G:
        case tgfx::SVGTag::Svg:
        case tgfx::SVGTag::Image:
        case tgfx::SVGTag::Use:
        case tgfx::SVGTag::Text: {
            const auto &transformable = static_cast<const tgfx::SVGTransformableNode &>(node);
            ApplySvgMatrixToLayer(layer, transformable.getTransform());
            break;
        }
        default:
            break;
    }
    ApplyNodeOpacity(layer, node);
}

void AddDiagnostic(SvgLayerTree &tree, const std::string &code, const std::string &message,
                   const std::string &nodeName) {
    Diagnostic diagnostic = {};
    diagnostic.code = code;
    diagnostic.message = message;
    diagnostic.nodeName = nodeName;
    tree.diagnostics.push_back(diagnostic);
}

struct WalkContext {
    SvgLayerTree *tree = nullptr;
    tgfx::SVGLengthContext lengthContext = tgfx::SVGLengthContext(tgfx::Size::Make(0.f, 0.f));
    const tgfx::SVGIDMapper *mapper = nullptr;
    std::vector<std::string> useStack = {};
    std::unordered_map<std::string, EntityId> imageAssetByHref = {};
};

void NetworkAabb(const VectorNetwork &network, Vec2 &minOut, Vec2 &sizeOut) {
    minOut = {0.f, 0.f};
    sizeOut = {0.f, 0.f};
    if (network.vertices.empty()) {
        return;
    }
    Vec2 min = network.vertices.front().point;
    Vec2 max = min;
    for (const VectorNetwork::Vertex &vertex : network.vertices) {
        min.x = std::min(min.x, vertex.point.x);
        min.y = std::min(min.y, vertex.point.y);
        max.x = std::max(max.x, vertex.point.x);
        max.y = std::max(max.y, vertex.point.y);
    }
    minOut = min;
    sizeOut = {max.x - min.x, max.y - min.y};
}

std::string LocalIriId(const tgfx::SVGIRI &iri) {
    std::string id = iri.iri();
    if (!id.empty() && id.front() == '#') {
        id = id.substr(1);
    }
    return id;
}

bool UseStackContains(const std::vector<std::string> &stack, const std::string &id) {
    for (const std::string &entry : stack) {
        if (entry == id) {
            return true;
        }
    }
    return false;
}

bool IsShapeTag(tgfx::SVGTag tag) {
    switch (tag) {
        case tgfx::SVGTag::Path:
        case tgfx::SVGTag::Rect:
        case tgfx::SVGTag::Circle:
        case tgfx::SVGTag::Ellipse:
        case tgfx::SVGTag::Line:
        case tgfx::SVGTag::Polygon:
        case tgfx::SVGTag::Polyline:
            return true;
        default:
            return false;
    }
}

int Base64Digit(char character) {
    if (character >= 'A' && character <= 'Z') {
        return character - 'A';
    }
    if (character >= 'a' && character <= 'z') {
        return character - 'a' + 26;
    }
    if (character >= '0' && character <= '9') {
        return character - '0' + 52;
    }
    if (character == '+') {
        return 62;
    }
    if (character == '/') {
        return 63;
    }
    return -1;
}

bool DecodeBase64(const std::string &input, std::vector<uint8_t> &out) {
    unsigned int accumulator = 0;
    int bits = 0;
    for (size_t i = 0; i < input.size(); ++i) {
        const char character = input[i];
        if (character == '=' || character == '\n' || character == '\r' || character == ' ') {
            continue;
        }
        const int digit = Base64Digit(character);
        if (digit < 0) {
            return false;
        }
        accumulator = (accumulator << 6) | static_cast<unsigned int>(digit);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xff));
        }
    }
    return !out.empty();
}

bool DecodeDataUriBytes(const std::string &iri, std::vector<uint8_t> &out) {
    const size_t marker = iri.find("base64,");
    if (marker == std::string::npos) {
        return false;
    }
    return DecodeBase64(iri.substr(marker + 7), out);
}

std::string HexEntityId(EntityId id) {
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(id.value));
    return buffer;
}

void ApplySkippedEffects(const tgfx::SVGNode &node, WalkContext &ctx) {
    const auto &filter = node.getFilter();
    if (filter.isValue() && filter->type() == tgfx::SVGFuncIRI::Type::IRI) {
        AddDiagnostic(*ctx.tree, "filter.skipped", "filter is not imported", LayerName(node));
    }
}

const tgfx::SVGNode *CollectSingleShape(const tgfx::SVGContainer &container) {
    const tgfx::SVGNode *shape = nullptr;
    size_t shapeCount = 0;
    for (const auto &child : container.getChildren()) {
        if (!child) {
            continue;
        }
        if (child->tag() == tgfx::SVGTag::Use || child->tag() == tgfx::SVGTag::G ||
            child->tag() == tgfx::SVGTag::Svg) {
            return nullptr;
        }
        if (IsShapeTag(child->tag())) {
            shape = child.get();
            shapeCount += 1;
        }
    }
    if (shapeCount != 1) {
        return nullptr;
    }
    return shape;
}

bool TryApplyUniformRoundedRectClip(Layer &layer, const tgfx::SVGNode &shape, WalkContext &ctx) {
    SvgRectGeom geom = {};
    if (!ResolveSvgRectGeom(shape, ctx.lengthContext, &geom) || !SvgRectHasUniformRadius(geom) ||
        geom.rx <= 0.f) {
        return false;
    }
    if (layer.type() == LayerType::Group) {
        auto *content = static_cast<NullContent *>(layer.content.get());
        content->cornerRadius.setStaticValue(geom.rx);
        return true;
    }
    if (layer.type() != LayerType::Image) {
        return false;
    }
    auto *content = static_cast<ImageContent *>(layer.content.get());
    const Vec2 size = content->size.staticValue();
    Mat3 inverse = {};
    if (!layer.localTransform(0).tryInvert(inverse)) {
        return false;
    }
    const Vec2 p00 = inverse.transformPoint({geom.x, geom.y});
    const Vec2 p10 = inverse.transformPoint({geom.x + geom.width, geom.y});
    const Vec2 p01 = inverse.transformPoint({geom.x, geom.y + geom.height});
    const Vec2 p11 = inverse.transformPoint({geom.x + geom.width, geom.y + geom.height});
    const Vec2 rxVec = inverse.transformVector({geom.rx, 0.f});
    const Vec2 ryVec = inverse.transformVector({0.f, geom.ry});
    float imageRadius = 0.f;
    if (!MappedRectMatchesImageBox(p00, p10, p01, p11, std::hypot(rxVec.x, rxVec.y),
                                   std::hypot(ryVec.x, ryVec.y), size.x, size.y, &imageRadius)) {
        return false;
    }
    content->cornerRadius.setStaticValue(imageRadius);
    return true;
}

bool AppendShapeMask(Layer &layer, const tgfx::SVGNode &shape, WalkContext &ctx) {
    bool usedConic = false;
    const VectorNetwork network =
        PathToVectorNetwork(ShapePathFromNode(shape, ctx.lengthContext), &usedConic);
    if (!NetworkHasArea(network)) {
        return false;
    }
    Mask mask = {};
    mask.path.setStaticValue(network);
    mask.mode = MaskMode::Add;
    layer.masks.push_back(mask);
    return true;
}

bool AppendMaskShapeLayer(const tgfx::SVGNode &shape, EntityId parentId, WalkContext &ctx,
                          EntityId *matteId) {
    if (matteId == nullptr) {
        return false;
    }
    SvgRectGeom geom = {};
    const bool uniformRect =
        ResolveSvgRectGeom(shape, ctx.lengthContext, &geom) && SvgRectHasUniformRadius(geom);
    VectorNetwork network = {};
    if (!uniformRect) {
        bool usedConic = false;
        network = PathToVectorNetwork(ShapePathFromNode(shape, ctx.lengthContext), &usedConic);
        if (!NetworkHasArea(network)) {
            return false;
        }
    }
    auto layer = std::make_unique<Layer>(LayerType::Shape);
    layer->name = LayerName(shape);
    layer->parentId = parentId;
    auto *content = static_cast<ShapeContent *>(layer->content.get());
    Vec2 boundsMin = {};
    Vec2 boundsSize = {};
    if (uniformRect) {
        auto geometry = std::make_unique<ShapeRect>();
        geometry->position.setStaticValue(
            {geom.x + geom.width * 0.5f, geom.y + geom.height * 0.5f});
        geometry->size.setStaticValue({geom.width, geom.height});
        geometry->cornerRadius.setStaticValue(geom.rx);
        content->geometry = std::move(geometry);
        boundsMin = {geom.x, geom.y};
        boundsSize = {geom.width, geom.height};
    } else {
        auto geometry = std::make_unique<ShapePath>();
        geometry->path.setStaticValue(network);
        content->geometry = std::move(geometry);
        NetworkAabb(network, boundsMin, boundsSize);
    }
    const ComputedStyle style = ResolveStyle(shape, ComputedStyle{}, ctx.lengthContext);
    ApplyPaintStyles(*layer, style, ctx.mapper, boundsMin, boundsSize, &ctx.tree->diagnostics);
    ApplyNodeTransform(*layer, shape);
    *matteId = layer->id;
    ctx.tree->layers.push_back(std::move(layer));
    return true;
}

void ApplyClipPath(Layer &layer, const tgfx::SVGNode &node, WalkContext &ctx) {
    const auto &clip = node.getClipPath();
    if (!clip.isValue() || clip->type() != tgfx::SVGFuncIRI::Type::IRI) {
        return;
    }
    if (ctx.mapper == nullptr) {
        AddDiagnostic(*ctx.tree, "clip.unsupported", "clip-path is too complex to import",
                      LayerName(node));
        return;
    }
    const auto it = ctx.mapper->find(LocalIriId(clip->iri()));
    if (it == ctx.mapper->end() || !it->second || it->second->tag() != tgfx::SVGTag::ClipPath) {
        AddDiagnostic(*ctx.tree, "clip.unsupported", "clip-path is too complex to import",
                      LayerName(node));
        return;
    }
    const auto &clipPath = static_cast<const tgfx::SVGClipPath &>(*it->second);
    const tgfx::SVGNode *shape = CollectSingleShape(clipPath);
    if (shape == nullptr) {
        AddDiagnostic(*ctx.tree, "clip.unsupported", "clip-path is too complex to import",
                      LayerName(node));
        return;
    }
    if (TryApplyUniformRoundedRectClip(layer, *shape, ctx)) {
        return;
    }
    if (!AppendShapeMask(layer, *shape, ctx)) {
        AddDiagnostic(*ctx.tree, "clip.unsupported", "clip-path is too complex to import",
                      LayerName(node));
    }
}

void ApplyMask(Layer &layer, const tgfx::SVGNode &node, WalkContext &ctx) {
    const auto &mask = node.getMask();
    if (!mask.isValue() || mask->type() != tgfx::SVGFuncIRI::Type::IRI) {
        return;
    }
    if (ctx.mapper == nullptr) {
        AddDiagnostic(*ctx.tree, "mask.skipped", "mask is not imported", LayerName(node));
        return;
    }
    const auto it = ctx.mapper->find(LocalIriId(mask->iri()));
    if (it == ctx.mapper->end() || !it->second || it->second->tag() != tgfx::SVGTag::Mask) {
        AddDiagnostic(*ctx.tree, "mask.skipped", "mask is not imported", LayerName(node));
        return;
    }
    const auto &svgMask = static_cast<const tgfx::SVGMask &>(*it->second);
    if (svgMask.getMaskContentUnits().type() !=
        tgfx::SVGObjectBoundingBoxUnits::Type::UserSpaceOnUse) {
        AddDiagnostic(*ctx.tree, "mask.skipped", "mask is not imported", LayerName(node));
        return;
    }
    const tgfx::SVGNode *shape = CollectSingleShape(svgMask);
    if (shape == nullptr) {
        AddDiagnostic(*ctx.tree, "mask.skipped", "mask is not imported", LayerName(node));
        return;
    }
    if (TryApplyUniformRoundedRectClip(layer, *shape, ctx)) {
        return;
    }
    EntityId matteId{};
    if (!AppendMaskShapeLayer(*shape, layer.parentId, ctx, &matteId)) {
        AddDiagnostic(*ctx.tree, "mask.skipped", "mask is not imported", LayerName(node));
        return;
    }
    const std::string maskName = LayerName(svgMask);
    if (!maskName.empty() && maskName != "Path") {
        ctx.tree->layers.back()->name = maskName;
    }
    layer.trackMatteLayerId = matteId;
    if (svgMask.getMaskType().type() == tgfx::SVGMaskType::Type::Alpha) {
        layer.trackMatteType = TrackMatteType::Alpha;
    } else {
        layer.trackMatteType = TrackMatteType::Luma;
    }
}

void ApplyNodeEffects(Layer &layer, const tgfx::SVGNode &node, WalkContext &ctx) {
    ApplyClipPath(layer, node, ctx);
    ApplyMask(layer, node, ctx);
    ApplySkippedEffects(node, ctx);
}

bool EnsureImageAsset(const tgfx::SVGIRI &href, const std::string &name, WalkContext &ctx,
                      EntityId *assetId) {
    if (assetId == nullptr || href.type() != tgfx::SVGIRI::Type::DataURI) {
        return false;
    }
    const auto cached = ctx.imageAssetByHref.find(href.iri());
    if (cached != ctx.imageAssetByHref.end()) {
        *assetId = cached->second;
        return true;
    }
    const tgfx::SVGImage::ImageInfo loaded =
        tgfx::SVGImage::LoadImage(href, tgfx::Rect::MakeWH(1.f, 1.f));
    if (!loaded.image) {
        return false;
    }
    std::vector<uint8_t> bytes;
    if (!DecodeDataUriBytes(href.iri(), bytes)) {
        return false;
    }
    Asset asset = {};
    asset.type = AssetType::Image;
    asset.name = name;
    asset.width = loaded.image->width();
    asset.height = loaded.image->height();
    asset.path = "assets/" + HexEntityId(asset.id) + ".png";
    EmbeddedImage embedded = {};
    embedded.assetId = asset.id;
    embedded.suggestedFileName = asset.path;
    embedded.bytes = std::move(bytes);
    *assetId = asset.id;
    ctx.imageAssetByHref[href.iri()] = asset.id;
    ctx.tree->assets.push_back(asset);
    ctx.tree->embeddedImages.push_back(std::move(embedded));
    return true;
}

tgfx::Matrix NodeTransformMatrix(const tgfx::SVGNode &node) {
    const tgfx::SVGTag tag = node.tag();
    switch (tag) {
        case tgfx::SVGTag::Path:
        case tgfx::SVGTag::Rect:
        case tgfx::SVGTag::Circle:
        case tgfx::SVGTag::Ellipse:
        case tgfx::SVGTag::Line:
        case tgfx::SVGTag::Polygon:
        case tgfx::SVGTag::Polyline:
        case tgfx::SVGTag::G:
        case tgfx::SVGTag::Svg:
        case tgfx::SVGTag::Image:
        case tgfx::SVGTag::Use:
        case tgfx::SVGTag::Text: {
            return static_cast<const tgfx::SVGTransformableNode &>(node).getTransform();
        }
        default: {
            tgfx::Matrix identity;
            identity.setIdentity();
            return identity;
        }
    }
}

bool PatternTileCoversBox(const tgfx::SVGPattern &pattern, Vec2 boundsSize,
                          const tgfx::SVGLengthContext &userContext) {
    const bool objectBox =
        pattern.getPatternUnits().type() == tgfx::SVGObjectBoundingBoxUnits::Type::ObjectBoundingBox;
    const tgfx::SVGLengthContext boxContext(tgfx::Size::Make(1.f, 1.f));
    const tgfx::SVGLengthContext &context = objectBox ? boxContext : userContext;
    float width = objectBox ? 1.f : boundsSize.x;
    float height = objectBox ? 1.f : boundsSize.y;
    if (pattern.getWidth().has_value()) {
        width = context.resolve(*pattern.getWidth(), tgfx::SVGLengthContext::LengthType::Horizontal);
    }
    if (pattern.getHeight().has_value()) {
        height = context.resolve(*pattern.getHeight(), tgfx::SVGLengthContext::LengthType::Vertical);
    }
    if (objectBox) {
        return width >= 0.999f && height >= 0.999f;
    }
    return width + 1e-3f >= boundsSize.x && height + 1e-3f >= boundsSize.y;
}

const tgfx::SVGImage *ResolvePatternImage(const tgfx::SVGPattern &pattern, WalkContext &ctx,
                                          const tgfx::SVGUse **useOut) {
    if (useOut != nullptr) {
        *useOut = nullptr;
    }
    const tgfx::SVGNode *found = nullptr;
    size_t count = 0;
    for (const auto &child : pattern.getChildren()) {
        if (!child) {
            continue;
        }
        if (child->tag() == tgfx::SVGTag::Image) {
            found = child.get();
            count += 1;
            continue;
        }
        if (child->tag() == tgfx::SVGTag::Use) {
            if (ctx.mapper == nullptr) {
                return nullptr;
            }
            const auto &use = static_cast<const tgfx::SVGUse &>(*child);
            const auto mapped = ctx.mapper->find(LocalIriId(use.getHref()));
            if (mapped == ctx.mapper->end() || !mapped->second ||
                mapped->second->tag() != tgfx::SVGTag::Image) {
                return nullptr;
            }
            found = mapped->second.get();
            if (useOut != nullptr) {
                *useOut = &use;
            }
            count += 1;
            continue;
        }
        return nullptr;
    }
    if (count != 1 || found == nullptr) {
        return nullptr;
    }
    return static_cast<const tgfx::SVGImage *>(found);
}

bool TryImportPatternFill(const tgfx::SVGNode &node, EntityId parentId, WalkContext &ctx,
                          const ComputedStyle &style, const VectorNetwork &network) {
    if (style.fillIri.empty() || ctx.mapper == nullptr) {
        return false;
    }
    const auto mapped = ctx.mapper->find(style.fillIri);
    if (mapped == ctx.mapper->end() || !mapped->second ||
        mapped->second->tag() != tgfx::SVGTag::Pattern) {
        return false;
    }
    const auto &pattern = static_cast<const tgfx::SVGPattern &>(*mapped->second);
    Vec2 boundsMin = {};
    Vec2 boundsSize = {};
    SvgRectGeom fillRect = {};
    const bool hasFillRect = ResolveSvgRectGeom(node, ctx.lengthContext, &fillRect);
    if (hasFillRect) {
        boundsMin = {fillRect.x, fillRect.y};
        boundsSize = {fillRect.width, fillRect.height};
    } else {
        NetworkAabb(network, boundsMin, boundsSize);
    }
    if (boundsSize.x <= 1e-6f || boundsSize.y <= 1e-6f) {
        return false;
    }
    if (!PatternTileCoversBox(pattern, boundsSize, ctx.lengthContext)) {
        return false;
    }
    const tgfx::SVGImage *image = ResolvePatternImage(pattern, ctx, nullptr);
    if (image == nullptr) {
        return false;
    }
    const tgfx::SVGIRI &href = image->getHref();
    if (href.type() != tgfx::SVGIRI::Type::DataURI) {
        return false;
    }
    EntityId assetId{};
    if (!EnsureImageAsset(href, LayerName(node), ctx, &assetId)) {
        AddDiagnostic(*ctx.tree, "image.decode", "data URI image could not be decoded",
                      LayerName(node));
        return false;
    }
    const bool uniformRect = hasFillRect && SvgRectHasUniformRadius(fillRect);
    auto layer = std::make_unique<Layer>(LayerType::Image);
    layer->name = LayerName(node);
    layer->parentId = parentId;
    layer->visible = style.visible;
    auto *content = static_cast<ImageContent *>(layer->content.get());
    content->assetId = assetId;
    content->size.setStaticValue(boundsSize);
    content->scaleMode = ImageScaleMode::Stretch;
    if (uniformRect) {
        content->cornerRadius.setStaticValue(fillRect.rx);
    }
    tgfx::Matrix layerMatrix = NodeTransformMatrix(node);
    layerMatrix.preConcat(tgfx::Matrix::MakeTrans(boundsMin.x, boundsMin.y));
    ApplySvgMatrixToLayer(*layer, layerMatrix);
    ApplyNodeOpacity(*layer, node);
    const float opacity = layer->transform.opacity.staticValue();
    layer->transform.opacity.setStaticValue(opacity * style.fillOpacity);
    if (!uniformRect) {
        tgfx::Path clipPath = ShapePathFromNode(node, ctx.lengthContext);
        clipPath.transform(tgfx::Matrix::MakeTrans(-boundsMin.x, -boundsMin.y));
        bool usedConic = false;
        const VectorNetwork maskNetwork = PathToVectorNetwork(clipPath, &usedConic);
        if (NetworkHasArea(maskNetwork)) {
            Mask mask = {};
            mask.path.setStaticValue(maskNetwork);
            mask.mode = MaskMode::Add;
            layer->masks.push_back(mask);
        }
    }
    ApplyNodeEffects(*layer, node, ctx);
    ctx.tree->layers.push_back(std::move(layer));
    return true;
}

void AddShapeLayer(const tgfx::SVGNode &node, EntityId parentId, WalkContext &ctx,
                   const ComputedStyle &style) {
    SvgRectGeom geom = {};
    const bool uniformRect =
        ResolveSvgRectGeom(node, ctx.lengthContext, &geom) && SvgRectHasUniformRadius(geom);
    VectorNetwork network = {};
    if (!uniformRect) {
        const tgfx::Path path = ShapePathFromNode(node, ctx.lengthContext);
        bool usedConic = false;
        network = PathToVectorNetwork(path, &usedConic);
        if (!NetworkHasArea(network) && !style.hasStroke) {
            AddDiagnostic(*ctx.tree, "shape.empty", "empty shape skipped", LayerName(node));
            return;
        }
    }
    ComputedStyle remaining = style;
    if (TryImportPatternFill(node, parentId, ctx, style, network)) {
        remaining.hasFill = false;
        remaining.fillIri.clear();
        if (!remaining.hasStroke) {
            return;
        }
    }
    auto layer = std::make_unique<Layer>(LayerType::Shape);
    layer->name = LayerName(node);
    layer->parentId = parentId;
    layer->visible = remaining.visible;
    auto *content = static_cast<ShapeContent *>(layer->content.get());
    Vec2 boundsMin = {};
    Vec2 boundsSize = {};
    if (uniformRect) {
        auto geometry = std::make_unique<ShapeRect>();
        geometry->position.setStaticValue(
            {geom.x + geom.width * 0.5f, geom.y + geom.height * 0.5f});
        geometry->size.setStaticValue({geom.width, geom.height});
        geometry->cornerRadius.setStaticValue(geom.rx);
        content->geometry = std::move(geometry);
        boundsMin = {geom.x, geom.y};
        boundsSize = {geom.width, geom.height};
    } else {
        auto geometry = std::make_unique<ShapePath>();
        geometry->path.setStaticValue(network);
        content->geometry = std::move(geometry);
        NetworkAabb(network, boundsMin, boundsSize);
    }
    ApplyPaintStyles(*layer, remaining, ctx.mapper, boundsMin, boundsSize, &ctx.tree->diagnostics);
    ApplyNodeTransform(*layer, node);
    ApplyNodeEffects(*layer, node, ctx);
    ctx.tree->layers.push_back(std::move(layer));
}

void WalkNode(const tgfx::SVGNode &node, EntityId parentId, WalkContext &ctx,
              const ComputedStyle &parentStyle);

void WalkChildren(const tgfx::SVGContainer &container, EntityId parentId, WalkContext &ctx,
                  const ComputedStyle &parentStyle) {
    for (const auto &child : container.getChildren()) {
        if (child) {
            WalkNode(*child, parentId, ctx, parentStyle);
        }
    }
}

void WalkUse(const tgfx::SVGUse &use, EntityId parentId, WalkContext &ctx,
             const ComputedStyle &style) {
    const std::string id = LocalIriId(use.getHref());
    if (id.empty() || ctx.mapper == nullptr) {
        AddDiagnostic(*ctx.tree, "use.missing", "use href could not be resolved", LayerName(use));
        return;
    }
    if (ctx.useStack.size() >= 32 || UseStackContains(ctx.useStack, id)) {
        AddDiagnostic(*ctx.tree, "use.cycle", "use reference cycle or depth limit", LayerName(use));
        return;
    }
    const auto it = ctx.mapper->find(id);
    if (it == ctx.mapper->end() || !it->second) {
        AddDiagnostic(*ctx.tree, "use.missing", "use href could not be resolved", LayerName(use));
        return;
    }
    auto group = std::make_unique<Layer>(LayerType::Group);
    group->name = LayerName(use);
    group->parentId = parentId;
    group->visible = style.visible;
    const float x =
        ctx.lengthContext.resolve(use.getX(), tgfx::SVGLengthContext::LengthType::Horizontal);
    const float y =
        ctx.lengthContext.resolve(use.getY(), tgfx::SVGLengthContext::LengthType::Vertical);
    tgfx::Matrix matrix = tgfx::Matrix::MakeTrans(x, y);
    matrix.preConcat(use.getTransform());
    ApplySvgMatrixToLayer(*group, matrix);
    ApplyNodeOpacity(*group, use);
    ApplyNodeEffects(*group, use, ctx);
    const EntityId groupId = group->id;
    ctx.tree->layers.push_back(std::move(group));
    ctx.useStack.push_back(id);
    WalkNode(*it->second, groupId, ctx, style);
    ctx.useStack.pop_back();
}

void WalkImage(const tgfx::SVGImage &image, EntityId parentId, WalkContext &ctx,
               const ComputedStyle &style) {
    const tgfx::SVGIRI &href = image.getHref();
    if (href.type() != tgfx::SVGIRI::Type::DataURI) {
        AddDiagnostic(*ctx.tree, "image.external", "external image href is not imported",
                      LayerName(image));
        return;
    }
    const float width =
        ctx.lengthContext.resolve(image.getWidth(), tgfx::SVGLengthContext::LengthType::Horizontal);
    const float height =
        ctx.lengthContext.resolve(image.getHeight(), tgfx::SVGLengthContext::LengthType::Vertical);
    EntityId assetId{};
    if (!EnsureImageAsset(href, LayerName(image), ctx, &assetId)) {
        AddDiagnostic(*ctx.tree, "image.decode", "data URI image could not be decoded",
                      LayerName(image));
        return;
    }
    auto layer = std::make_unique<Layer>(LayerType::Image);
    layer->name = LayerName(image);
    layer->parentId = parentId;
    layer->visible = style.visible;
    auto *content = static_cast<ImageContent *>(layer->content.get());
    content->assetId = assetId;
    content->size.setStaticValue({width, height});
    if (image.getPreserveAspectRatio().align == tgfx::SVGPreserveAspectRatio::Align::None) {
        content->scaleMode = ImageScaleMode::Stretch;
    } else {
        content->scaleMode = ImageScaleMode::LetterBox;
    }
    const float x =
        ctx.lengthContext.resolve(image.getX(), tgfx::SVGLengthContext::LengthType::Horizontal);
    const float y =
        ctx.lengthContext.resolve(image.getY(), tgfx::SVGLengthContext::LengthType::Vertical);
    tgfx::Matrix matrix = tgfx::Matrix::MakeTrans(x, y);
    matrix.preConcat(image.getTransform());
    ApplySvgMatrixToLayer(*layer, matrix);
    ApplyNodeOpacity(*layer, image);
    ApplyNodeEffects(*layer, image, ctx);
    ctx.tree->layers.push_back(std::move(layer));
}

void WalkNode(const tgfx::SVGNode &node, EntityId parentId, WalkContext &ctx,
              const ComputedStyle &parentStyle) {
    const ComputedStyle style = ResolveStyle(node, parentStyle, ctx.lengthContext);
    if (style.displayNone) {
        return;
    }
    const tgfx::SVGTag tag = node.tag();
    if (IsSkippedContainer(tag) || IsFilterPrimitive(tag)) {
        return;
    }
    if (tag == tgfx::SVGTag::Text) {
        ImportSvgText(static_cast<const tgfx::SVGText &>(node), parentId, *ctx.tree,
                      ctx.lengthContext, ctx.mapper, parentStyle);
        return;
    }
    if (tag == tgfx::SVGTag::TextPath) {
        AddDiagnostic(*ctx.tree, "textPath.skipped", "textPath is not imported", LayerName(node));
        return;
    }
    if (IsTextNode(tag)) {
        return;
    }
    if (tag == tgfx::SVGTag::G || tag == tgfx::SVGTag::Svg) {
        auto group = std::make_unique<Layer>(LayerType::Group);
        group->name = LayerName(node);
        group->parentId = parentId;
        group->visible = style.visible;
        ApplyNodeTransform(*group, node);
        ApplyNodeEffects(*group, node, ctx);
        const EntityId groupId = group->id;
        ctx.tree->layers.push_back(std::move(group));
        WalkChildren(static_cast<const tgfx::SVGContainer &>(node), groupId, ctx, style);
        return;
    }
    switch (tag) {
        case tgfx::SVGTag::Path:
        case tgfx::SVGTag::Rect:
        case tgfx::SVGTag::Circle:
        case tgfx::SVGTag::Ellipse:
        case tgfx::SVGTag::Line:
        case tgfx::SVGTag::Polygon:
        case tgfx::SVGTag::Polyline:
            AddShapeLayer(node, parentId, ctx, style);
            break;
        case tgfx::SVGTag::Use:
            WalkUse(static_cast<const tgfx::SVGUse &>(node), parentId, ctx, style);
            break;
        case tgfx::SVGTag::Image:
            WalkImage(static_cast<const tgfx::SVGImage &>(node), parentId, ctx, style);
            break;
        default:
            AddDiagnostic(*ctx.tree, "tag.unknown", "unsupported SVG element skipped",
                          LayerName(node));
            break;
    }
}

}  // namespace

void WalkSvgRoot(const tgfx::SVGRoot &root, const tgfx::SVGIDMapper &mapper, SvgLayerTree &tree) {
    if (tree.layers.empty()) {
        return;
    }
    WalkContext ctx = {};
    ctx.tree = &tree;
    ctx.lengthContext = MakeRootLengthContext(tree.sourceWidth, tree.sourceHeight);
    ctx.mapper = &mapper;
    ComputedStyle rootStyle = {};
    WalkChildren(root, tree.layers.front()->id, ctx, rootStyle);
}

}  // namespace svg
}  // namespace motion
