#include "MotionStudio/common/VectorNetworkConvert.h"

#include "MotionStudio/common/GeometryRevision.h"
#include <unordered_map>
#include <utility>

namespace motion {

namespace {

const VectorNetwork::Vertex *VertexById(const VectorNetwork &network, uint32_t id) {
    return FindVertex(network, id);
}

VectorNetwork ContourToVectorNetwork(const BezierPath::Contour &contour) {
    VectorNetwork network;
    if (contour.vertices.empty()) {
        return network;
    }
    network.vertices.reserve(contour.vertices.size());
    for (size_t i = 0; i < contour.vertices.size(); ++i) {
        network.vertices.push_back({static_cast<uint32_t>(i + 1), contour.vertices[i].point});
    }
    const size_t count = contour.vertices.size();
    const size_t edgeCount = contour.closed ? count : (count >= 1 ? count - 1 : 0);
    network.edges.reserve(edgeCount);
    for (size_t i = 0; i < edgeCount; ++i) {
        const size_t next = (i + 1) % count;
        VectorNetwork::Edge edge;
        edge.id = static_cast<uint32_t>(i + 1);
        edge.start = network.vertices[i].id;
        edge.end = network.vertices[next].id;
        edge.startTangent = contour.vertices[i].outTangent;
        edge.endTangent = contour.vertices[next].inTangent;
        network.edges.push_back(edge);
    }
    GeometryRevisionAccess::Stamp(network);
    return network;
}

}  // namespace

VectorNetwork BezierPathToVectorNetwork(const BezierPath &path) {
    if (path.contours.size() != 1) {
        return {};
    }
    return ContourToVectorNetwork(path.contours.front());
}

BezierPath VectorNetworkToSingleRingBezierPath(const VectorNetwork &network) {
    if (network.vertices.empty()) {
        return {};
    }
    if (network.edges.empty()) {
        if (network.vertices.size() == 1) {
            return MakeSingleContour({{network.vertices.front().point, {}, {}}}, false);
        }
        return {};
    }

    // Build undirected adjacency: only degree <= 2 allowed for a simple ring/chain.
    std::unordered_map<uint32_t, std::vector<uint32_t>> adjacency;
    for (const VectorNetwork::Edge &edge : network.edges) {
        if (edge.start == edge.end) {
            return {};
        }
        adjacency[edge.start].push_back(edge.id);
        adjacency[edge.end].push_back(edge.id);
    }
    for (const auto &entry : adjacency) {
        if (entry.second.size() > 2) {
            return {};
        }
    }
    if (adjacency.size() != network.vertices.size()) {
        return {};
    }

    uint32_t startId = network.vertices.front().id;
    for (const VectorNetwork::Vertex &vertex : network.vertices) {
        if (adjacency[vertex.id].size() == 1) {
            startId = vertex.id;
            break;
        }
    }

    std::vector<uint32_t> orderedIds;
    orderedIds.reserve(network.vertices.size());
    uint32_t current = startId;
    uint32_t previousEdge = 0;
    while (orderedIds.size() < network.vertices.size()) {
        orderedIds.push_back(current);
        const std::vector<uint32_t> &incident = adjacency[current];
        uint32_t nextEdge = 0;
        for (uint32_t edgeId : incident) {
            if (edgeId != previousEdge) {
                nextEdge = edgeId;
                break;
            }
        }
        if (nextEdge == 0) {
            break;
        }
        const VectorNetwork::Edge *edge = FindEdge(network, nextEdge);
        if (edge == nullptr) {
            return {};
        }
        const uint32_t nextVertex = edge->start == current ? edge->end : edge->start;
        previousEdge = nextEdge;
        current = nextVertex;
        if (current == startId) {
            break;
        }
    }
    if (orderedIds.size() != network.vertices.size()) {
        return {};
    }

    const bool closed = adjacency[startId].size() == 2;
    std::vector<BezierPath::Vertex> vertices;
    vertices.reserve(orderedIds.size());
    for (uint32_t id : orderedIds) {
        const VectorNetwork::Vertex *vertex = VertexById(network, id);
        if (vertex == nullptr) {
            return {};
        }
        vertices.push_back({vertex->point, {}, {}});
    }

    const size_t count = orderedIds.size();
    const size_t edgeCount = closed ? count : (count >= 1 ? count - 1 : 0);
    for (size_t i = 0; i < edgeCount; ++i) {
        const uint32_t fromId = orderedIds[i];
        const uint32_t toId = orderedIds[(i + 1) % count];
        const VectorNetwork::Edge *matched = nullptr;
        for (const VectorNetwork::Edge &edge : network.edges) {
            if ((edge.start == fromId && edge.end == toId) ||
                (edge.start == toId && edge.end == fromId)) {
                matched = &edge;
                break;
            }
        }
        if (matched == nullptr) {
            return {};
        }
        if (matched->start == fromId && matched->end == toId) {
            vertices[i].outTangent = matched->startTangent;
            vertices[(i + 1) % count].inTangent = matched->endTangent;
        } else {
            vertices[i].outTangent = matched->endTangent;
            vertices[(i + 1) % count].inTangent = matched->startTangent;
        }
    }
    return MakeSingleContour(std::move(vertices), closed);
}

}  // namespace motion
