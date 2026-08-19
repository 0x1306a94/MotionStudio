#include "SvgTransform.h"

#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/VectorNetwork.h"
#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapePath.h"

namespace motion {
namespace svg {

namespace {

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
    layer.transform.anchorPoint.setStaticValue(anchor);
    layer.transform.position.setStaticValue({translation.x + anchor.x, translation.y + anchor.y});
}

}  // namespace

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
