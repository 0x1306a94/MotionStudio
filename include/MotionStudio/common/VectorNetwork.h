#pragma once

#include <cstdint>
#include <vector>

#include "MotionStudio/common/Vec2.h"

namespace motion {

// Figma-style handle mirroring at a vertex (Inspector Mirroring control).
enum class VertexMirrorMode : uint8_t {
    None = 0,
    Angle = 1,
    AngleLength = 2,
};

// Authoring graph for pen paths: shared vertices connected by cubic edges.
// Tangents live on edges (Lottie-relative offsets from the endpoint).
struct VectorNetwork {
    struct Vertex {
        uint32_t id = 0;
        Vec2 point = {};
        VertexMirrorMode mirrorMode = VertexMirrorMode::None;

        bool operator==(const Vertex &other) const;
        bool operator!=(const Vertex &other) const;
    };

    struct Edge {
        uint32_t id = 0;
        uint32_t start = 0;
        uint32_t end = 0;
        // Control point = start.point + startTangent.
        Vec2 startTangent = {};
        // Control point = end.point + endTangent.
        Vec2 endTangent = {};

        bool operator==(const Edge &other) const;
        bool operator!=(const Edge &other) const;
    };

    std::vector<Vertex> vertices;
    std::vector<Edge> edges;

    bool operator==(const VectorNetwork &other) const;
    bool operator!=(const VectorNetwork &other) const;
};

// Counts edges that touch vertexId as start or end. Returns 0 if missing.
int VertexDegree(const VectorNetwork &network, uint32_t vertexId);

// Returns nullptr when id is absent.
const VectorNetwork::Vertex *FindVertex(const VectorNetwork &network, uint32_t id);
VectorNetwork::Vertex *FindVertex(VectorNetwork &network, uint32_t id);

const VectorNetwork::Edge *FindEdge(const VectorNetwork &network, uint32_t id);
VectorNetwork::Edge *FindEdge(VectorNetwork &network, uint32_t id);

// Next unused positive id (max existing + 1, or 1 when empty).
uint32_t AllocVertexId(const VectorNetwork &network);
uint32_t AllocEdgeId(const VectorNetwork &network);

}  // namespace motion
