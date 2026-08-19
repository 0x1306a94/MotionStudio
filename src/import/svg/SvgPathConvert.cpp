#include "SvgPathConvert.h"

#include "MotionStudio/common/Vec2.h"
#include "tgfx/core/PathTypes.h"

namespace motion {
namespace svg {

namespace {

constexpr float kZeroLengthEpsilon = 1e-6f;

Vec2 ToVec2(const tgfx::Point &point) {
    return {point.x, point.y};
}

bool PointsNear(Vec2 left, Vec2 right) {
    return ApproxEqual(left, right, kZeroLengthEpsilon);
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
    Vec2 contourStartPoint = {};
    tgfx::Path::Iterator it = path.begin();
    const tgfx::Path::Iterator end = path.end();
    while (it != end) {
        const tgfx::Path::Segment &segment = *it;
        switch (segment.verb) {
            case tgfx::PathVerb::Move: {
                contourStartPoint = ToVec2(segment.points[0]);
                contourStartId = AppendVertex(network, nextVertexId, contourStartPoint);
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
                if (contourStartId != 0 && currentId != 0) {
                    const VectorNetwork::Vertex *current = FindVertex(network, currentId);
                    if (current != nullptr && PointsNear(current->point, contourStartPoint)) {
                        if (currentId != contourStartId) {
                            network.vertices.pop_back();
                            currentId = contourStartId;
                            if (!network.edges.empty() &&
                                network.edges.back().end == current->id) {
                                network.edges.back().end = contourStartId;
                            }
                        }
                    }
                    AppendEdge(network, nextEdgeId, currentId, contourStartId, {}, {});
                }
                break;
            }
            case tgfx::PathVerb::Done: {
                break;
            }
        }
        ++it;
    }
    return network;
}

}  // namespace svg
}  // namespace motion
