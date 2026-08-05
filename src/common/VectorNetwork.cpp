#include "MotionStudio/common/VectorNetwork.h"

#include <algorithm>

namespace motion {

bool VectorNetwork::Vertex::operator==(const Vertex &other) const {
    return id == other.id && point == other.point;
}

bool VectorNetwork::Vertex::operator!=(const Vertex &other) const {
    return !(*this == other);
}

bool VectorNetwork::Edge::operator==(const Edge &other) const {
    return id == other.id && start == other.start && end == other.end &&
        startTangent == other.startTangent && endTangent == other.endTangent;
}

bool VectorNetwork::Edge::operator!=(const Edge &other) const {
    return !(*this == other);
}

bool VectorNetwork::operator==(const VectorNetwork &other) const {
    return vertices == other.vertices && edges == other.edges;
}

bool VectorNetwork::operator!=(const VectorNetwork &other) const {
    return !(*this == other);
}

int VertexDegree(const VectorNetwork &network, uint32_t vertexId) {
    int degree = 0;
    for (const VectorNetwork::Edge &edge : network.edges) {
        if (edge.start == vertexId || edge.end == vertexId) {
            ++degree;
        }
    }
    return degree;
}

const VectorNetwork::Vertex *FindVertex(const VectorNetwork &network, uint32_t id) {
    for (const VectorNetwork::Vertex &vertex : network.vertices) {
        if (vertex.id == id) {
            return &vertex;
        }
    }
    return nullptr;
}

VectorNetwork::Vertex *FindVertex(VectorNetwork &network, uint32_t id) {
    for (VectorNetwork::Vertex &vertex : network.vertices) {
        if (vertex.id == id) {
            return &vertex;
        }
    }
    return nullptr;
}

const VectorNetwork::Edge *FindEdge(const VectorNetwork &network, uint32_t id) {
    for (const VectorNetwork::Edge &edge : network.edges) {
        if (edge.id == id) {
            return &edge;
        }
    }
    return nullptr;
}

VectorNetwork::Edge *FindEdge(VectorNetwork &network, uint32_t id) {
    for (VectorNetwork::Edge &edge : network.edges) {
        if (edge.id == id) {
            return &edge;
        }
    }
    return nullptr;
}

uint32_t AllocVertexId(const VectorNetwork &network) {
    uint32_t maxId = 0;
    for (const VectorNetwork::Vertex &vertex : network.vertices) {
        maxId = std::max(maxId, vertex.id);
    }
    return maxId + 1;
}

uint32_t AllocEdgeId(const VectorNetwork &network) {
    uint32_t maxId = 0;
    for (const VectorNetwork::Edge &edge : network.edges) {
        maxId = std::max(maxId, edge.id);
    }
    return maxId + 1;
}

}  // namespace motion
