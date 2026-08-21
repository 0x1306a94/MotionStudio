#include "MotionStudio/common/VectorNetworkEdit.h"

#include "MotionStudio/common/GeometryRevision.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace motion {

namespace {

constexpr float kEpsilon = 1e-4f;

float LengthSquared(Vec2 value) {
    return value.x * value.x + value.y * value.y;
}

bool IsNearZero(Vec2 value) {
    return LengthSquared(value) <= kEpsilon * kEpsilon;
}

struct SegmentSplit {
    Vec2 leftStartTangent = {};
    Vec2 midPoint = {};
    Vec2 midStartTangent = {};
    Vec2 midEndTangent = {};
    Vec2 rightEndTangent = {};
};

SegmentSplit SplitCubic(Vec2 p0, Vec2 startTangent, Vec2 endTangent, Vec2 p3, float t) {
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    const Vec2 c1 = p0 + startTangent;
    const Vec2 c2 = p3 + endTangent;
    const Vec2 a = p0 + (c1 - p0) * clamped;
    const Vec2 b = c1 + (c2 - c1) * clamped;
    const Vec2 c = c2 + (p3 - c2) * clamped;
    const Vec2 d = a + (b - a) * clamped;
    const Vec2 e = b + (c - b) * clamped;
    const Vec2 f = d + (e - d) * clamped;

    SegmentSplit split;
    split.leftStartTangent = a - p0;
    split.midPoint = f;
    split.midStartTangent = e - f;
    split.midEndTangent = d - f;
    split.rightEndTangent = c - p3;
    return split;
}

bool HasUndirectedEdge(const VectorNetwork &network, uint32_t start, uint32_t end) {
    for (const VectorNetwork::Edge &edge : network.edges) {
        if ((edge.start == start && edge.end == end) || (edge.start == end && edge.end == start)) {
            return true;
        }
    }
    return false;
}

// Returns the other edge id incident to vertexId, or 0 if not exactly degree 2.
uint32_t OtherIncidentEdgeId(const VectorNetwork &network, uint32_t vertexId, uint32_t excludeEdgeId) {
    if (VertexDegree(network, vertexId) != 2) {
        return 0;
    }
    for (const VectorNetwork::Edge &edge : network.edges) {
        if (edge.id == excludeEdgeId) {
            continue;
        }
        if (edge.start == vertexId || edge.end == vertexId) {
            return edge.id;
        }
    }
    return 0;
}

void SetHandleAtVertex(VectorNetwork::Edge &edge, uint32_t vertexId, Vec2 tangent) {
    if (edge.start == vertexId) {
        edge.startTangent = tangent;
    } else if (edge.end == vertexId) {
        edge.endTangent = tangent;
    }
}

Vec2 HandleAtVertex(const VectorNetwork::Edge &edge, uint32_t vertexId) {
    if (edge.start == vertexId) {
        return edge.startTangent;
    }
    if (edge.end == vertexId) {
        return edge.endTangent;
    }
    return {};
}

Vec2 SafeNormalize(Vec2 value) {
    const float length = std::sqrt(LengthSquared(value));
    if (length <= kEpsilon) {
        return {};
    }
    return value * (1.0f / length);
}

}  // namespace

VectorNetwork AddVertex(VectorNetwork network, Vec2 point, uint32_t *outId) {
    const uint32_t id = AllocVertexId(network);
    network.vertices.push_back({id, point});
    if (outId != nullptr) {
        *outId = id;
    }
    GeometryRevisionAccess::Stamp(network);
    return network;
}

VectorNetwork AddEdge(VectorNetwork network, uint32_t start, uint32_t end, uint32_t *outId) {
    if (outId != nullptr) {
        *outId = 0;
    }
    if (start == end || FindVertex(network, start) == nullptr ||
        FindVertex(network, end) == nullptr) {
        return network;
    }
    if (HasUndirectedEdge(network, start, end)) {
        return network;
    }
    const uint32_t id = AllocEdgeId(network);
    network.edges.push_back({id, start, end, {}, {}});
    if (outId != nullptr) {
        *outId = id;
    }
    GeometryRevisionAccess::Stamp(network);
    return network;
}

VectorNetwork MoveVertex(VectorNetwork network, uint32_t id, Vec2 point) {
    VectorNetwork::Vertex *vertex = FindVertex(network, id);
    if (vertex == nullptr) {
        return network;
    }
    vertex->point = point;
    GeometryRevisionAccess::Stamp(network);
    return network;
}

VectorNetwork MoveEdgeTangent(VectorNetwork network, uint32_t edgeId, bool atStart, Vec2 tangent,
                              bool mirror) {
    VectorNetwork::Edge *edge = FindEdge(network, edgeId);
    if (edge == nullptr) {
        return network;
    }
    const uint32_t vertexId = atStart ? edge->start : edge->end;
    if (atStart) {
        edge->startTangent = tangent;
    } else {
        edge->endTangent = tangent;
    }
    GeometryRevisionAccess::Stamp(network);
    if (!mirror) {
        return network;
    }
    const VectorNetwork::Vertex *vertex = FindVertex(network, vertexId);
    if (vertex == nullptr || vertex->mirrorMode == VertexMirrorMode::None) {
        return network;
    }
    const uint32_t otherId = OtherIncidentEdgeId(network, vertexId, edgeId);
    if (otherId == 0) {
        return network;
    }
    VectorNetwork::Edge *other = FindEdge(network, otherId);
    if (other == nullptr) {
        return network;
    }
    if (vertex->mirrorMode == VertexMirrorMode::AngleLength) {
        SetHandleAtVertex(*other, vertexId, -tangent);
        return network;
    }
    // Angle: opposite direction, preserve opposite length (or use dragged length).
    const Vec2 opposite = HandleAtVertex(*other, vertexId);
    const float length =
        IsNearZero(opposite) ? std::sqrt(LengthSquared(tangent)) : std::sqrt(LengthSquared(opposite));
    const Vec2 direction = SafeNormalize(-tangent);
    SetHandleAtVertex(*other, vertexId, direction * length);
    return network;
}

VectorNetwork InsertVertexOnEdge(VectorNetwork network, uint32_t edgeId, float t, uint32_t *outId) {
    if (outId != nullptr) {
        *outId = 0;
    }
    VectorNetwork::Edge *edge = FindEdge(network, edgeId);
    if (edge == nullptr) {
        return network;
    }
    const VectorNetwork::Vertex *start = FindVertex(network, edge->start);
    const VectorNetwork::Vertex *end = FindVertex(network, edge->end);
    if (start == nullptr || end == nullptr) {
        return network;
    }

    const SegmentSplit split =
        SplitCubic(start->point, edge->startTangent, edge->endTangent, end->point, t);
    const uint32_t midId = AllocVertexId(network);
    network.vertices.push_back({midId, split.midPoint});

    const uint32_t oldStart = edge->start;
    const uint32_t oldEnd = edge->end;
    const uint32_t leftId = edge->id;
    // Reuse the original edge as the left half; append the right half.
    edge->end = midId;
    edge->startTangent = split.leftStartTangent;
    edge->endTangent = split.midEndTangent;

    const uint32_t rightId = AllocEdgeId(network);
    network.edges.push_back(
        {rightId, midId, oldEnd, split.midStartTangent, split.rightEndTangent});
    (void)oldStart;
    (void)leftId;
    if (outId != nullptr) {
        *outId = midId;
    }
    GeometryRevisionAccess::Stamp(network);
    return network;
}

VectorNetwork RemoveVertex(VectorNetwork network, uint32_t id) {
    if (FindVertex(network, id) == nullptr) {
        return network;
    }
    std::vector<VectorNetwork::Edge> kept;
    kept.reserve(network.edges.size());
    for (const VectorNetwork::Edge &edge : network.edges) {
        if (edge.start != id && edge.end != id) {
            kept.push_back(edge);
        }
    }
    network.edges = std::move(kept);

    std::vector<VectorNetwork::Vertex> vertices;
    vertices.reserve(network.vertices.size());
    for (const VectorNetwork::Vertex &vertex : network.vertices) {
        if (vertex.id != id) {
            vertices.push_back(vertex);
        }
    }
    network.vertices = std::move(vertices);
    GeometryRevisionAccess::Stamp(network);
    return network;
}

VectorNetwork RemoveEdge(VectorNetwork network, uint32_t id) {
    std::vector<VectorNetwork::Edge> kept;
    kept.reserve(network.edges.size());
    for (const VectorNetwork::Edge &edge : network.edges) {
        if (edge.id != id) {
            kept.push_back(edge);
        }
    }
    if (kept.size() == network.edges.size()) {
        return network;
    }
    network.edges = std::move(kept);
    GeometryRevisionAccess::Stamp(network);
    return network;
}

VectorNetwork SetVertexMirrorMode(VectorNetwork network, uint32_t vertexId, VertexMirrorMode mode) {
    VectorNetwork::Vertex *vertex = FindVertex(network, vertexId);
    if (vertex == nullptr) {
        return network;
    }
    vertex->mirrorMode = mode;
    GeometryRevisionAccess::Stamp(network);
    if (VertexDegree(network, vertexId) != 2) {
        return network;
    }

    VectorNetwork::Edge *incident[2] = {nullptr, nullptr};
    int incidentCount = 0;
    for (VectorNetwork::Edge &edge : network.edges) {
        if (edge.start != vertexId && edge.end != vertexId) {
            continue;
        }
        if (incidentCount < 2) {
            incident[incidentCount++] = &edge;
        }
    }
    if (incidentCount != 2 || incident[0] == nullptr || incident[1] == nullptr) {
        return network;
    }

    if (mode == VertexMirrorMode::None) {
        SetHandleAtVertex(*incident[0], vertexId, {});
        SetHandleAtVertex(*incident[1], vertexId, {});
        return network;
    }

    // Prefer directed chain: incoming (end==v) + outgoing (start==v).
    VectorNetwork::Edge *incoming = nullptr;
    VectorNetwork::Edge *outgoing = nullptr;
    for (int i = 0; i < 2; ++i) {
        if (incident[i]->end == vertexId) {
            incoming = incident[i];
        }
        if (incident[i]->start == vertexId) {
            outgoing = incident[i];
        }
    }

    const Vec2 point = vertex->point;
    if (incoming != nullptr && outgoing != nullptr && incoming != outgoing) {
        const VectorNetwork::Vertex *prev = FindVertex(network, incoming->start);
        const VectorNetwork::Vertex *next = FindVertex(network, outgoing->end);
        if (prev == nullptr || next == nullptr) {
            return network;
        }
        const Vec2 direction = SafeNormalize(next->point - prev->point);
        if (IsNearZero(direction)) {
            return network;
        }
        float inLength = std::sqrt(LengthSquared(point - prev->point)) / 3.0f;
        float outLength = std::sqrt(LengthSquared(next->point - point)) / 3.0f;
        if (mode == VertexMirrorMode::AngleLength) {
            const float shared = (inLength + outLength) * 0.5f;
            inLength = shared;
            outLength = shared;
        }
        incoming->endTangent = direction * (-inLength);
        outgoing->startTangent = direction * outLength;
        return network;
    }

    const uint32_t other0 =
        incident[0]->start == vertexId ? incident[0]->end : incident[0]->start;
    const uint32_t other1 =
        incident[1]->start == vertexId ? incident[1]->end : incident[1]->start;
    const VectorNetwork::Vertex *neighbor0 = FindVertex(network, other0);
    const VectorNetwork::Vertex *neighbor1 = FindVertex(network, other1);
    if (neighbor0 == nullptr || neighbor1 == nullptr) {
        return network;
    }
    const Vec2 direction = SafeNormalize(neighbor1->point - neighbor0->point);
    if (IsNearZero(direction)) {
        return network;
    }
    float length0 = std::sqrt(LengthSquared(point - neighbor0->point)) / 3.0f;
    float length1 = std::sqrt(LengthSquared(neighbor1->point - point)) / 3.0f;
    if (mode == VertexMirrorMode::AngleLength) {
        const float shared = (length0 + length1) * 0.5f;
        length0 = shared;
        length1 = shared;
    }
    SetHandleAtVertex(*incident[0], vertexId, direction * (-length0));
    SetHandleAtVertex(*incident[1], vertexId, direction * length1);
    return network;
}

VectorNetwork ToggleVertexSmooth(VectorNetwork network, uint32_t vertexId) {
    const VectorNetwork::Vertex *vertex = FindVertex(network, vertexId);
    if (vertex == nullptr || VertexDegree(network, vertexId) != 2) {
        return network;
    }
    const VectorNetwork::Edge *incident[2] = {nullptr, nullptr};
    int incidentCount = 0;
    for (const VectorNetwork::Edge &edge : network.edges) {
        if (edge.start != vertexId && edge.end != vertexId) {
            continue;
        }
        if (incidentCount < 2) {
            incident[incidentCount++] = &edge;
        }
    }
    if (incidentCount != 2) {
        return network;
    }
    const bool isCorner = IsNearZero(HandleAtVertex(*incident[0], vertexId)) &&
        IsNearZero(HandleAtVertex(*incident[1], vertexId));
    return SetVertexMirrorMode(network, vertexId,
                               isCorner ? VertexMirrorMode::Angle : VertexMirrorMode::None);
}

VectorNetwork RecenterNetwork(VectorNetwork network, Vec2 &localCenterOut) {
    localCenterOut = {};
    if (network.vertices.empty()) {
        return network;
    }
    Vec2 minPoint = network.vertices.front().point;
    Vec2 maxPoint = minPoint;
    for (const VectorNetwork::Vertex &vertex : network.vertices) {
        minPoint.x = std::min(minPoint.x, vertex.point.x);
        minPoint.y = std::min(minPoint.y, vertex.point.y);
        maxPoint.x = std::max(maxPoint.x, vertex.point.x);
        maxPoint.y = std::max(maxPoint.y, vertex.point.y);
    }
    const Vec2 center{(minPoint.x + maxPoint.x) * 0.5f, (minPoint.y + maxPoint.y) * 0.5f};
    if (IsNearZero(center)) {
        return network;
    }
    localCenterOut = center;
    for (VectorNetwork::Vertex &vertex : network.vertices) {
        vertex.point = vertex.point - center;
    }
    GeometryRevisionAccess::Stamp(network);
    return network;
}

}  // namespace motion
