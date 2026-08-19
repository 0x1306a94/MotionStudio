#include "SvgWalk.h"

#include <algorithm>
#include <vector>

#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapePath.h"
#include "SvgLength.h"
#include "SvgPathConvert.h"
#include "SvgStyle.h"
#include "SvgTransform.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Size.h"
#include "tgfx/svg/node/SVGCircle.h"
#include "tgfx/svg/node/SVGContainer.h"
#include "tgfx/svg/node/SVGEllipse.h"
#include "tgfx/svg/node/SVGLine.h"
#include "tgfx/svg/node/SVGNode.h"
#include "tgfx/svg/node/SVGPath.h"
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
            const auto &rect = static_cast<const tgfx::SVGRect &>(node);
            const float x =
                lengthContext.resolve(rect.getX(), tgfx::SVGLengthContext::LengthType::Horizontal);
            const float y =
                lengthContext.resolve(rect.getY(), tgfx::SVGLengthContext::LengthType::Vertical);
            const float width = lengthContext.resolve(rect.getWidth(),
                                                      tgfx::SVGLengthContext::LengthType::Horizontal);
            const float height = lengthContext.resolve(
                rect.getHeight(), tgfx::SVGLengthContext::LengthType::Vertical);
            if (width <= 0.f || height <= 0.f) {
                break;
            }
            const tgfx::Rect bounds = tgfx::Rect::MakeXYWH(x, y, width, height);
            float rx = 0.f;
            float ry = 0.f;
            if (rect.getRx().has_value()) {
                rx = lengthContext.resolve(*rect.getRx(),
                                           tgfx::SVGLengthContext::LengthType::Horizontal);
            }
            if (rect.getRy().has_value()) {
                ry = lengthContext.resolve(*rect.getRy(),
                                           tgfx::SVGLengthContext::LengthType::Vertical);
            }
            if (rx <= 0.f && ry > 0.f) {
                rx = ry;
            }
            if (ry <= 0.f && rx > 0.f) {
                ry = rx;
            }
            rx = std::min(rx, width * 0.5f);
            ry = std::min(ry, height * 0.5f);
            if (rx > 0.f || ry > 0.f) {
                path.addRoundRect(bounds, rx, ry);
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

void AddShapeLayer(const tgfx::SVGNode &node, EntityId parentId, WalkContext &ctx,
                   const ComputedStyle &style) {
    const tgfx::Path path = ShapePathFromNode(node, ctx.lengthContext);
    bool usedConic = false;
    const VectorNetwork network = PathToVectorNetwork(path, &usedConic);
    if (!NetworkHasArea(network) && !style.hasStroke) {
        AddDiagnostic(*ctx.tree, "shape.empty", "empty shape skipped", LayerName(node));
        return;
    }
    auto layer = std::make_unique<Layer>(LayerType::Shape);
    layer->name = LayerName(node);
    layer->parentId = parentId;
    layer->visible = style.visible;
    auto *content = static_cast<ShapeContent *>(layer->content.get());
    auto geometry = std::make_unique<ShapePath>();
    geometry->path.setStaticValue(network);
    content->geometry = std::move(geometry);
    Vec2 boundsMin = {};
    Vec2 boundsSize = {};
    NetworkAabb(network, boundsMin, boundsSize);
    ApplyPaintStyles(*layer, style, ctx.mapper, boundsMin, boundsSize, &ctx.tree->diagnostics);
    ApplyNodeTransform(*layer, node);
    if (style.hasDash) {
        AddDiagnostic(*ctx.tree, "stroke.dash", "stroke-dasharray is imported as a solid stroke",
                      layer->name);
    }
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
    const EntityId groupId = group->id;
    ctx.tree->layers.push_back(std::move(group));
    ctx.useStack.push_back(id);
    WalkNode(*it->second, groupId, ctx, style);
    ctx.useStack.pop_back();
}

void WalkNode(const tgfx::SVGNode &node, EntityId parentId, WalkContext &ctx,
              const ComputedStyle &parentStyle) {
    const ComputedStyle style = ResolveStyle(node, parentStyle, ctx.lengthContext);
    if (style.displayNone) {
        return;
    }
    const tgfx::SVGTag tag = node.tag();
    if (IsSkippedContainer(tag) || IsFilterPrimitive(tag) || IsTextNode(tag)) {
        return;
    }
    if (tag == tgfx::SVGTag::G || tag == tgfx::SVGTag::Svg) {
        auto group = std::make_unique<Layer>(LayerType::Group);
        group->name = LayerName(node);
        group->parentId = parentId;
        group->visible = style.visible;
        ApplyNodeTransform(*group, node);
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
