#include "SvgTransform.h"

#include <cmath>

#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/VectorNetwork.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapePath.h"
#include "tgfx/core/Rect.h"
#include "tgfx/svg/SVGTypes.h"

namespace motion {
namespace svg {

namespace {

constexpr float kShearEpsilon = 1e-3f;
constexpr float kSingularEpsilon = 1e-8f;

Vec2 MapPoint(const tgfx::Matrix &matrix, Vec2 point) {
    const tgfx::Point mapped = matrix.mapXY(point.x, point.y);
    return {mapped.x, mapped.y};
}

void TransformNetwork(VectorNetwork &network, const tgfx::Matrix &matrix) {
    std::vector<Vec2> originalPoints;
    originalPoints.reserve(network.vertices.size());
    for (const VectorNetwork::Vertex &vertex : network.vertices) {
        originalPoints.push_back(vertex.point);
    }
    for (VectorNetwork::Vertex &vertex : network.vertices) {
        vertex.point = MapPoint(matrix, vertex.point);
    }
    for (VectorNetwork::Edge &edge : network.edges) {
        const VectorNetwork::Vertex *start = FindVertex(network, edge.start);
        const VectorNetwork::Vertex *end = FindVertex(network, edge.end);
        if (start == nullptr || end == nullptr) {
            continue;
        }
        Vec2 startOriginal = {};
        Vec2 endOriginal = {};
        for (size_t i = 0; i < network.vertices.size(); ++i) {
            if (network.vertices[i].id == edge.start) {
                startOriginal = originalPoints[i];
            }
            if (network.vertices[i].id == edge.end) {
                endOriginal = originalPoints[i];
            }
        }
        const Vec2 startControl = MapPoint(matrix, startOriginal + edge.startTangent);
        const Vec2 endControl = MapPoint(matrix, endOriginal + edge.endTangent);
        edge.startTangent = startControl - start->point;
        edge.endTangent = endControl - end->point;
    }
}

bool NetworkBounds(const VectorNetwork &network, Vec2 &minOut, Vec2 &maxOut) {
    if (network.vertices.empty()) {
        return false;
    }
    minOut = network.vertices.front().point;
    maxOut = minOut;
    for (const VectorNetwork::Vertex &vertex : network.vertices) {
        if (vertex.point.x < minOut.x) {
            minOut.x = vertex.point.x;
        }
        if (vertex.point.y < minOut.y) {
            minOut.y = vertex.point.y;
        }
        if (vertex.point.x > maxOut.x) {
            maxOut.x = vertex.point.x;
        }
        if (vertex.point.y > maxOut.y) {
            maxOut.y = vertex.point.y;
        }
    }
    return minOut.x <= maxOut.x && minOut.y <= maxOut.y;
}

bool LayerLocalBounds(const Layer &layer, Vec2 &minOut, Vec2 &maxOut) {
    if (layer.type() != LayerType::Shape) {
        return false;
    }
    auto *content = static_cast<const ShapeContent *>(layer.content.get());
    if (content == nullptr || content->geometry == nullptr ||
        content->geometry->type() != ShapeType::Path) {
        return false;
    }
    auto *path = static_cast<const ShapePath *>(content->geometry.get());
    return NetworkBounds(path->path.staticValue(), minOut, maxOut);
}

bool GroupBounds(const std::vector<std::unique_ptr<Layer>> &layers, const Layer &group, Vec2 &minOut,
                 Vec2 &maxOut) {
    bool hasBounds = false;
    for (const auto &child : layers) {
        if (child->parentId != group.id) {
            continue;
        }
        Vec2 childMin = {};
        Vec2 childMax = {};
        if (!LayerLocalBounds(*child, childMin, childMax) &&
            !GroupBounds(layers, *child, childMin, childMax)) {
            continue;
        }
        const Mat3 local = child->localTransform(0);
        const Vec2 corners[4] = {{childMin.x, childMin.y},
                                 {childMax.x, childMin.y},
                                 {childMax.x, childMax.y},
                                 {childMin.x, childMax.y}};
        for (const Vec2 &corner : corners) {
            const Vec2 mapped = local.transformPoint(corner);
            if (!hasBounds) {
                minOut = mapped;
                maxOut = mapped;
                hasBounds = true;
            } else {
                if (mapped.x < minOut.x) {
                    minOut.x = mapped.x;
                }
                if (mapped.y < minOut.y) {
                    minOut.y = mapped.y;
                }
                if (mapped.x > maxOut.x) {
                    maxOut.x = mapped.x;
                }
                if (mapped.y > maxOut.y) {
                    maxOut.y = mapped.y;
                }
            }
        }
    }
    return hasBounds;
}

void AssignLayerCenterAnchor(Layer &layer, Vec2 minPoint, Vec2 maxPoint) {
    const Vec2 anchor{(minPoint.x + maxPoint.x) * 0.5f, (minPoint.y + maxPoint.y) * 0.5f};
    const Vec2 translation = layer.transform.position.staticValue();
    const Vec2 scale = layer.transform.scale.staticValue();
    const float rotation = layer.transform.rotation.staticValue();
    const Mat3 rotatedScale = Mat3::Rotate(rotation) * Mat3::Scale(scale);
    const Vec2 offset = rotatedScale.transformPoint(anchor);
    layer.transform.anchorPoint.setStaticValue(anchor);
    layer.transform.position.setStaticValue({translation.x + offset.x, translation.y + offset.y});
}

tgfx::Matrix TrsMatrix(const DecomposedTransform &decomp) {
    return tgfx::Matrix::MakeTrans(decomp.translation.x, decomp.translation.y) *
        tgfx::Matrix::MakeRotate(decomp.rotationDegrees) *
        tgfx::Matrix::MakeScale(decomp.scale.x, decomp.scale.y);
}

tgfx::Matrix ComputeViewboxMatrix(const tgfx::Rect &viewBox, const tgfx::Rect &viewPort,
                                  const tgfx::SVGPreserveAspectRatio &aspect) {
    if (viewBox.isEmpty() || viewPort.isEmpty()) {
        return tgfx::Matrix::MakeScale(0.f, 0.f);
    }
    const float scaleX = viewPort.width() / viewBox.width();
    const float scaleY = viewPort.height() / viewBox.height();
    float sx = scaleX;
    float sy = scaleY;
    if (aspect.align != tgfx::SVGPreserveAspectRatio::Align::None) {
        const float uniform = aspect.scale == tgfx::SVGPreserveAspectRatio::Scale::Meet
            ? std::min(scaleX, scaleY)
            : std::max(scaleX, scaleY);
        sx = uniform;
        sy = uniform;
    }
    const float alignCoeffs[3] = {0.f, 0.5f, 1.f};
    const size_t xCoeff = static_cast<size_t>(static_cast<int>(aspect.align) >> 0 & 0x03);
    const size_t yCoeff = static_cast<size_t>(static_cast<int>(aspect.align) >> 2 & 0x03);
    const float tx = -viewBox.x() * sx;
    const float ty = -viewBox.y() * sy;
    const float dx = viewPort.width() - viewBox.width() * sx;
    const float dy = viewPort.height() - viewBox.height() * sy;
    return tgfx::Matrix::MakeTrans(tx + dx * alignCoeffs[xCoeff], ty + dy * alignCoeffs[yCoeff]) *
        tgfx::Matrix::MakeScale(sx, sy);
}

void WriteTrs(Layer &layer, const DecomposedTransform &decomp) {
    layer.transform.position.setStaticValue(decomp.translation);
    layer.transform.scale.setStaticValue(decomp.scale);
    layer.transform.rotation.setStaticValue(decomp.rotationDegrees);
}

}  // namespace

DecomposedTransform DecomposeSvgMatrix(const tgfx::Matrix &matrix) {
    const float a = matrix.getScaleX();
    const float b = matrix.getSkewY();
    const float c = matrix.getSkewX();
    const float d = matrix.getScaleY();
    DecomposedTransform out = {};
    out.translation = {matrix.getTranslateX(), matrix.getTranslateY()};
    const float scaleX = std::hypot(a, b);
    const float det = a * d - b * c;
    const float scaleY = std::hypot(c, d) * (det < 0.f ? -1.f : 1.f);
    out.scale = {scaleX, scaleY};
    out.rotationDegrees = std::atan2(b, a) * 180.f / static_cast<float>(M_PI);
    out.singular = std::fabs(det) < kSingularEpsilon;
    if (scaleX > kSingularEpsilon && std::fabs(scaleY) > kSingularEpsilon) {
        const float nx = a / scaleX;
        const float ny = b / scaleX;
        const float mx = c / scaleY;
        const float my = d / scaleY;
        const float shearDot = std::fabs(nx * mx + ny * my);
        out.hasShear = shearDot > kShearEpsilon;
    }
    return out;
}

void ApplyResidualBake(Layer &layer, const tgfx::Matrix &residual) {
    if (layer.type() != LayerType::Shape) {
        return;
    }
    auto *content = static_cast<ShapeContent *>(layer.content.get());
    if (content == nullptr || content->geometry == nullptr ||
        content->geometry->type() != ShapeType::Path) {
        return;
    }
    auto *path = static_cast<ShapePath *>(content->geometry.get());
    VectorNetwork network = path->path.staticValue();
    TransformNetwork(network, residual);
    path->path.setStaticValue(network);
}

void BakeResidualIntoDescendants(std::vector<std::unique_ptr<Layer>> &layers, EntityId parentId,
                                 const tgfx::Matrix &residual) {
    for (auto &layer : layers) {
        if (layer->parentId != parentId) {
            continue;
        }
        ApplyResidualBake(*layer, residual);
        BakeResidualIntoDescendants(layers, layer->id, residual);
    }
}

void ApplySvgMatrixToLayer(Layer &layer, const tgfx::Matrix &matrix) {
    if (matrix.isIdentity()) {
        return;
    }
    const DecomposedTransform decomp = DecomposeSvgMatrix(matrix);
    if (decomp.singular) {
        ApplyResidualBake(layer, matrix);
        return;
    }
    if (decomp.hasShear) {
        tgfx::Matrix trs = TrsMatrix(decomp);
        tgfx::Matrix inverse = {};
        if (trs.invert(&inverse)) {
            ApplyResidualBake(layer, matrix * inverse);
        } else {
            ApplyResidualBake(layer, matrix);
            return;
        }
    }
    WriteTrs(layer, decomp);
}

void ApplyRootViewBoxAndTransform(Layer &root, const tgfx::SVGRoot &svgRoot, int sourceWidth,
                                  int sourceHeight) {
    tgfx::Matrix matrix;
    if (svgRoot.getViewBox().has_value()) {
        const tgfx::Rect viewPort =
            tgfx::Rect::MakeWH(static_cast<float>(sourceWidth), static_cast<float>(sourceHeight));
        matrix = ComputeViewboxMatrix(*svgRoot.getViewBox(), viewPort,
                                      svgRoot.getPreserveAspectRatio());
    }
    matrix = svgRoot.getTransform() * matrix;
    ApplySvgMatrixToLayer(root, matrix);
    const auto &opacity = svgRoot.getOpacity();
    if (opacity.isValue()) {
        root.transform.opacity.setStaticValue(*opacity);
    }
}

void AssignCenterAnchors(std::vector<std::unique_ptr<Layer>> &layers) {
    if (layers.empty()) {
        return;
    }
    for (size_t i = layers.size(); i > 0; --i) {
        Layer &layer = *layers[i - 1];
        Vec2 minPoint = {};
        Vec2 maxPoint = {};
        bool hasBounds = false;
        if (layer.type() == LayerType::Shape) {
            hasBounds = LayerLocalBounds(layer, minPoint, maxPoint);
        } else if (layer.type() == LayerType::Group) {
            hasBounds = GroupBounds(layers, layer, minPoint, maxPoint);
        }
        if (!hasBounds) {
            layer.transform.anchorPoint.setStaticValue({0.f, 0.f});
            continue;
        }
        AssignLayerCenterAnchor(layer, minPoint, maxPoint);
    }
}

}  // namespace svg
}  // namespace motion
