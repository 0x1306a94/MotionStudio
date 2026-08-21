#include "SvgPathConvert.h"

#include "MotionStudio/common/GeometryRevision.h"
#include "MotionStudio/common/Vec2.h"
#include "tgfx/core/PathTypes.h"

namespace motion {
namespace svg {

namespace {

constexpr float kZeroLengthEpsilon = 1e-6f;
// Pattern inverse / viewBox transforms leave Close's final line a few ulps
// long. Skipping that line without keeping currentId on the previous vertex
// (or welding it to the start) yields an open network; CompileFillFaces then
// emits no face and an Add mask hides the layer.
constexpr float CLOSE_WELD_EPSILON = 1e-3f;

Vec2 ToVec2(const tgfx::Point &point) {
    return {point.x, point.y};
}

bool PointsNear(Vec2 left, Vec2 right, float epsilon = kZeroLengthEpsilon) {
    return ApproxEqual(left, right, epsilon);
}

void WeldCurrentToContourStart(VectorNetwork &network, uint32_t &currentId,
                               uint32_t contourStartId) {
    if (currentId == 0 || contourStartId == 0 || currentId == contourStartId) {
        return;
    }
    const VectorNetwork::Vertex *current = FindVertex(network, currentId);
    const VectorNetwork::Vertex *start = FindVertex(network, contourStartId);
    if (current == nullptr || start == nullptr) {
        return;
    }
    if (!PointsNear(current->point, start->point, CLOSE_WELD_EPSILON)) {
        return;
    }
    const uint32_t removedId = current->id;
    if (!network.edges.empty() && network.edges.back().end == removedId) {
        network.edges.back().end = contourStartId;
    }
    if (!network.vertices.empty() && network.vertices.back().id == removedId) {
        network.vertices.pop_back();
    }
    currentId = contourStartId;
}

uint32_t AppendVertex(VectorNetwork &network, uint32_t &nextId, Vec2 point) {
    const uint32_t id = nextId;
    nextId += 1;
    VectorNetwork::Vertex vertex = {};
    vertex.id = id;
    vertex.point = point;
    vertex.mirrorMode = VertexMirrorMode::None;
    network.vertices.push_back(vertex);
    return id;
}

void AppendEdge(VectorNetwork &network, uint32_t &nextId, uint32_t start, uint32_t end,
                Vec2 startTangent, Vec2 endTangent) {
    if (start == 0 || end == 0 || start == end) {
        return;
    }
    const VectorNetwork::Vertex *startVertex = FindVertex(network, start);
    const VectorNetwork::Vertex *endVertex = FindVertex(network, end);
    if (startVertex == nullptr || endVertex == nullptr) {
        return;
    }
    if (PointsNear(startVertex->point, endVertex->point) && PointsNear(startTangent, {}) &&
        PointsNear(endTangent, {})) {
        return;
    }
    VectorNetwork::Edge edge = {};
    edge.id = nextId;
    nextId += 1;
    edge.start = start;
    edge.end = end;
    edge.startTangent = startTangent;
    edge.endTangent = endTangent;
    network.edges.push_back(edge);
}

void AppendCubic(VectorNetwork &network, uint32_t &nextVertexId, uint32_t &nextEdgeId,
                 uint32_t &currentId, Vec2 p0, Vec2 c1, Vec2 c2, Vec2 p3) {
    if (currentId != 0) {
        const VectorNetwork::Vertex *current = FindVertex(network, currentId);
        if (current != nullptr && PointsNear(current->point, p3, CLOSE_WELD_EPSILON) &&
            PointsNear(c1 - p0, {}) && PointsNear(c2 - p3, {})) {
            return;
        }
    }
    const uint32_t endId = AppendVertex(network, nextVertexId, p3);
    AppendEdge(network, nextEdgeId, currentId, endId, c1 - p0, c2 - p3);
    currentId = endId;
}

// One-cubic approximation of a conic (p0, q, p2, w):
// k = (4/3) * w / (1 + w); C1 = p0 + k(q - p0); C2 = p2 + k(q - p2).
// When w == 1 this matches the standard quad-to-cubic elevation (k = 2/3).
void AppendConicAsCubic(VectorNetwork &network, uint32_t &nextVertexId, uint32_t &nextEdgeId,
                        uint32_t &currentId, bool *usedConic, Vec2 p0, Vec2 q, Vec2 p2,
                        float weight) {
    if (usedConic != nullptr) {
        *usedConic = true;
    }
    const float denom = 1.f + weight;
    float k = 2.f / 3.f;
    if (denom > kZeroLengthEpsilon) {
        k = (4.f / 3.f) * weight / denom;
    }
    const Vec2 c1 = p0 + (q - p0) * k;
    const Vec2 c2 = p2 + (q - p2) * k;
    AppendCubic(network, nextVertexId, nextEdgeId, currentId, p0, c1, c2, p2);
}

}  // namespace

VectorNetwork PathToVectorNetwork(const tgfx::Path &path, bool *usedConic) {
    if (usedConic != nullptr) {
        *usedConic = false;
    }
    VectorNetwork network = {};
    uint32_t nextVertexId = 1;
    uint32_t nextEdgeId = 1;
    uint32_t contourStartId = 0;
    uint32_t currentId = 0;
    tgfx::Path::Iterator it = path.begin();
    const tgfx::Path::Iterator end = path.end();
    while (it != end) {
        const tgfx::Path::Segment &segment = *it;
        switch (segment.verb) {
            case tgfx::PathVerb::Move: {
                WeldCurrentToContourStart(network, currentId, contourStartId);
                contourStartId = AppendVertex(network, nextVertexId, ToVec2(segment.points[0]));
                currentId = contourStartId;
                break;
            }
            case tgfx::PathVerb::Line: {
                const Vec2 endPoint = ToVec2(segment.points[1]);
                AppendCubic(network, nextVertexId, nextEdgeId, currentId, ToVec2(segment.points[0]),
                            ToVec2(segment.points[0]), endPoint, endPoint);
                break;
            }
            case tgfx::PathVerb::Quad: {
                const Vec2 p0 = ToVec2(segment.points[0]);
                const Vec2 q = ToVec2(segment.points[1]);
                const Vec2 p2 = ToVec2(segment.points[2]);
                const Vec2 c1 = p0 + (q - p0) * (2.f / 3.f);
                const Vec2 c2 = p2 + (q - p2) * (2.f / 3.f);
                AppendCubic(network, nextVertexId, nextEdgeId, currentId, p0, c1, c2, p2);
                break;
            }
            case tgfx::PathVerb::Conic: {
                AppendConicAsCubic(network, nextVertexId, nextEdgeId, currentId, usedConic,
                                   ToVec2(segment.points[0]), ToVec2(segment.points[1]),
                                   ToVec2(segment.points[2]), segment.conicWeight);
                break;
            }
            case tgfx::PathVerb::Cubic: {
                AppendCubic(network, nextVertexId, nextEdgeId, currentId, ToVec2(segment.points[0]),
                            ToVec2(segment.points[1]), ToVec2(segment.points[2]),
                            ToVec2(segment.points[3]));
                break;
            }
            case tgfx::PathVerb::Close: {
                WeldCurrentToContourStart(network, currentId, contourStartId);
                AppendEdge(network, nextEdgeId, currentId, contourStartId, {}, {});
                break;
            }
            case tgfx::PathVerb::Done: {
                break;
            }
        }
        ++it;
    }
    WeldCurrentToContourStart(network, currentId, contourStartId);
    GeometryRevisionAccess::Stamp(network);
    return network;
}

}  // namespace svg
}  // namespace motion
