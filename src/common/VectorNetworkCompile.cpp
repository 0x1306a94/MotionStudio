#include "MotionStudio/common/VectorNetworkCompile.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace motion {

namespace {

struct HalfEdge {
    size_t edgeIndex = 0;
    // True: traverse edge.start -> edge.end; false: reverse.
    bool forward = true;
};

struct Outgoing {
    HalfEdge half;
    float angle = 0.0f;
};

struct FaceWalk {
    std::vector<HalfEdge> halves;
    float signedArea = 0.0f;
};

bool HalfEdgeEqual(const HalfEdge &left, const HalfEdge &right) {
    return left.edgeIndex == right.edgeIndex && left.forward == right.forward;
}

HalfEdge Twin(const HalfEdge &half) {
    HalfEdge twin;
    twin.edgeIndex = half.edgeIndex;
    twin.forward = !half.forward;
    return twin;
}

uint32_t HalfFrom(const VectorNetwork &network, const HalfEdge &half) {
    const VectorNetwork::Edge &edge = network.edges[half.edgeIndex];
    return half.forward ? edge.start : edge.end;
}

uint32_t HalfTo(const VectorNetwork &network, const HalfEdge &half) {
    const VectorNetwork::Edge &edge = network.edges[half.edgeIndex];
    return half.forward ? edge.end : edge.start;
}

Vec2 OutgoingDirection(const VectorNetwork &network, const HalfEdge &half) {
    const VectorNetwork::Edge &edge = network.edges[half.edgeIndex];
    const VectorNetwork::Vertex *start = FindVertex(network, edge.start);
    const VectorNetwork::Vertex *end = FindVertex(network, edge.end);
    if (start == nullptr || end == nullptr) {
        return {};
    }
    // Face walking must use chord directions only. Cubic tangents bend the
    // stroke but must not reorder the combinatorial embedding — otherwise
    // left-turn traversal collapses interiors into one outer face.
    if (half.forward) {
        return end->point - start->point;
    }
    return start->point - end->point;
}

float AngleOf(const Vec2 &direction) {
    return std::atan2(direction.y, direction.x);
}

float LengthSquared(Vec2 value) {
    return value.x * value.x + value.y * value.y;
}

bool OutgoingAngleLess(const Outgoing &left, const Outgoing &right) {
    if (left.angle != right.angle) {
        return left.angle < right.angle;
    }
    if (left.half.edgeIndex != right.half.edgeIndex) {
        return left.half.edgeIndex < right.half.edgeIndex;
    }
    return left.half.forward < right.half.forward;
}

bool IsUsed(const std::vector<uint8_t> &used, const HalfEdge &half) {
    const size_t slot = half.edgeIndex * 2 + (half.forward ? 0 : 1);
    return used[slot] != 0;
}

void MarkUsed(std::vector<uint8_t> &used, const HalfEdge &half) {
    const size_t slot = half.edgeIndex * 2 + (half.forward ? 0 : 1);
    used[slot] = 1;
}

float PolygonSignedArea(const std::vector<Vec2> &points) {
    if (points.size() < 3) {
        return 0.0f;
    }
    float area = 0.0f;
    for (size_t i = 0; i < points.size(); ++i) {
        const Vec2 &a = points[i];
        const Vec2 &b = points[(i + 1) % points.size()];
        area += a.x * b.y - b.x * a.y;
    }
    return area * 0.5f;
}

HalfEdge NextLeftTurn(const std::unordered_map<uint32_t, std::vector<Outgoing>> &adjacency,
                      uint32_t vertexId, const HalfEdge &incoming) {
    const auto found = adjacency.find(vertexId);
    if (found == adjacency.end() || found->second.empty()) {
        return Twin(incoming);
    }
    const std::vector<Outgoing> &outgoing = found->second;
    const HalfEdge twin = Twin(incoming);
    size_t twinIndex = 0;
    bool foundTwin = false;
    for (size_t i = 0; i < outgoing.size(); ++i) {
        if (HalfEdgeEqual(outgoing[i].half, twin)) {
            twinIndex = i;
            foundTwin = true;
            break;
        }
    }
    if (!foundTwin) {
        return twin;
    }
    // Outgoing sorted CCW by angle; previous entry is the sharpest left turn.
    const size_t nextIndex = (twinIndex + outgoing.size() - 1) % outgoing.size();
    return outgoing[nextIndex].half;
}

FaceWalk WalkFace(const VectorNetwork &network,
                  const std::unordered_map<uint32_t, std::vector<Outgoing>> &adjacency,
                  std::vector<uint8_t> &used, const HalfEdge &start) {
    FaceWalk walk;
    HalfEdge current = start;
    const size_t edgeLimit = network.edges.size() * 4 + 4;
    for (size_t step = 0; step < edgeLimit; ++step) {
        if (IsUsed(used, current)) {
            walk.halves.clear();
            return walk;
        }
        MarkUsed(used, current);
        walk.halves.push_back(current);
        const uint32_t toId = HalfTo(network, current);
        current = NextLeftTurn(adjacency, toId, current);
        if (HalfEdgeEqual(current, start)) {
            break;
        }
    }
    if (walk.halves.empty() || !HalfEdgeEqual(current, start)) {
        walk.halves.clear();
        return walk;
    }

    std::vector<Vec2> points;
    points.reserve(walk.halves.size());
    for (const HalfEdge &half : walk.halves) {
        const VectorNetwork::Vertex *from = FindVertex(network, HalfFrom(network, half));
        if (from == nullptr) {
            walk.halves.clear();
            return walk;
        }
        points.push_back(from->point);
    }
    walk.signedArea = PolygonSignedArea(points);
    return walk;
}

BezierPath::Contour ContourFromFace(const VectorNetwork &network, const FaceWalk &face) {
    BezierPath::Contour contour;
    contour.closed = true;
    if (face.halves.empty()) {
        return contour;
    }
    contour.vertices.resize(face.halves.size());
    for (size_t i = 0; i < face.halves.size(); ++i) {
        const HalfEdge &half = face.halves[i];
        const VectorNetwork::Edge &edge = network.edges[half.edgeIndex];
        const uint32_t fromId = HalfFrom(network, half);
        const uint32_t toId = HalfTo(network, half);
        const VectorNetwork::Vertex *from = FindVertex(network, fromId);
        const VectorNetwork::Vertex *to = FindVertex(network, toId);
        if (from == nullptr || to == nullptr) {
            return {};
        }
        contour.vertices[i].point = from->point;
        const size_t nextIndex = (i + 1) % face.halves.size();
        contour.vertices[nextIndex].point = to->point;
        if (half.forward) {
            contour.vertices[i].outTangent = edge.startTangent;
            contour.vertices[nextIndex].inTangent = edge.endTangent;
        } else {
            contour.vertices[i].outTangent = edge.endTangent;
            contour.vertices[nextIndex].inTangent = edge.startTangent;
        }
    }
    return contour;
}

uint64_t UndirectedPairKey(uint32_t a, uint32_t b) {
    const uint32_t lo = std::min(a, b);
    const uint32_t hi = std::max(a, b);
    return (static_cast<uint64_t>(lo) << 32) | static_cast<uint64_t>(hi);
}

// First-seen directed edge wins for each undirected {start,end} pair.
std::vector<size_t> UniqueUndirectedEdgeIndices(const VectorNetwork &network) {
    std::vector<size_t> indices;
    indices.reserve(network.edges.size());
    std::unordered_map<uint64_t, size_t> seen;
    seen.reserve(network.edges.size());
    for (size_t edgeIndex = 0; edgeIndex < network.edges.size(); ++edgeIndex) {
        const VectorNetwork::Edge &edge = network.edges[edgeIndex];
        if (edge.start == edge.end) {
            continue;
        }
        if (FindVertex(network, edge.start) == nullptr || FindVertex(network, edge.end) == nullptr) {
            continue;
        }
        const uint64_t key = UndirectedPairKey(edge.start, edge.end);
        if (seen.find(key) != seen.end()) {
            continue;
        }
        seen.emplace(key, edgeIndex);
        indices.push_back(edgeIndex);
    }
    return indices;
}

int FindRoot(std::vector<int> &parent, int index) {
    int root = index;
    while (parent[static_cast<size_t>(root)] != root) {
        root = parent[static_cast<size_t>(root)];
    }
    int current = index;
    while (parent[static_cast<size_t>(current)] != root) {
        const int next = parent[static_cast<size_t>(current)];
        parent[static_cast<size_t>(current)] = root;
        current = next;
    }
    return root;
}

void UnionRoots(std::vector<int> &parent, int left, int right) {
    const int leftRoot = FindRoot(parent, left);
    const int rightRoot = FindRoot(parent, right);
    if (leftRoot != rightRoot) {
        parent[static_cast<size_t>(rightRoot)] = leftRoot;
    }
}

// Drop the largest-|area| face in each connected component (the local outer).
std::vector<uint8_t> OuterFaceMask(const VectorNetwork &network,
                                   const std::vector<size_t> &activeEdges,
                                   const std::vector<FaceWalk> &faces) {
    std::vector<uint8_t> isOuter(faces.size(), 0);
    if (faces.empty()) {
        return isOuter;
    }

    std::unordered_map<uint32_t, int> vertexIndex;
    vertexIndex.reserve(network.vertices.size());
    for (size_t i = 0; i < network.vertices.size(); ++i) {
        vertexIndex.emplace(network.vertices[i].id, static_cast<int>(i));
    }
    std::vector<int> parent(network.vertices.size());
    for (size_t i = 0; i < parent.size(); ++i) {
        parent[i] = static_cast<int>(i);
    }
    for (size_t edgeIndex : activeEdges) {
        const VectorNetwork::Edge &edge = network.edges[edgeIndex];
        const auto startFound = vertexIndex.find(edge.start);
        const auto endFound = vertexIndex.find(edge.end);
        if (startFound == vertexIndex.end() || endFound == vertexIndex.end()) {
            continue;
        }
        UnionRoots(parent, startFound->second, endFound->second);
    }

    std::unordered_map<int, std::vector<size_t>> facesByComponent;
    for (size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex) {
        if (faces[faceIndex].halves.empty()) {
            continue;
        }
        const uint32_t vertexId = HalfFrom(network, faces[faceIndex].halves.front());
        const auto found = vertexIndex.find(vertexId);
        if (found == vertexIndex.end()) {
            continue;
        }
        const int root = FindRoot(parent, found->second);
        facesByComponent[root].push_back(faceIndex);
    }

    for (const auto &entry : facesByComponent) {
        const std::vector<size_t> &indices = entry.second;
        size_t outerIndex = indices.front();
        for (size_t i = 1; i < indices.size(); ++i) {
            const size_t candidate = indices[i];
            const float absArea = std::fabs(faces[candidate].signedArea);
            const float outerAbs = std::fabs(faces[outerIndex].signedArea);
            if (absArea > outerAbs + 1e-6f) {
                outerIndex = candidate;
            } else if (std::fabs(absArea - outerAbs) <= 1e-6f &&
                       faces[candidate].signedArea < faces[outerIndex].signedArea) {
                outerIndex = candidate;
            }
        }
        isOuter[outerIndex] = 1;
    }
    return isOuter;
}

constexpr int kSamplesPerEdge = 16;
constexpr float kMergeEps = 1e-3f;

Vec2 EvalCubic(Vec2 p0, Vec2 c1, Vec2 c2, Vec2 p3, float t) {
    const float u = 1.0f - t;
    return p0 * (u * u * u) + c1 * (3.0f * u * u * t) + c2 * (3.0f * u * t * t) +
        p3 * (t * t * t);
}

bool AlmostEqualPoint(Vec2 left, Vec2 right) {
    return LengthSquared(left - right) <= kMergeEps * kMergeEps;
}

uint64_t QuantizePoint(Vec2 point) {
    const int32_t qx = static_cast<int32_t>(std::llround(point.x / kMergeEps));
    const int32_t qy = static_cast<int32_t>(std::llround(point.y / kMergeEps));
    return (static_cast<uint64_t>(static_cast<uint32_t>(qx)) << 32) |
        static_cast<uint32_t>(qy);
}

bool IntersectSegments(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Vec2 &out, float &tAB, float &tCD) {
    const Vec2 r = b - a;
    const Vec2 s = d - c;
    const float denom = r.x * s.y - r.y * s.x;
    if (std::fabs(denom) <= 1e-12f) {
        return false;
    }
    const Vec2 qp = c - a;
    const float t = (qp.x * s.y - qp.y * s.x) / denom;
    const float u = (qp.x * r.y - qp.y * r.x) / denom;
    if (t < -1e-5f || t > 1.0f + 1e-5f || u < -1e-5f || u > 1.0f + 1e-5f) {
        return false;
    }
    tAB = std::clamp(t, 0.0f, 1.0f);
    tCD = std::clamp(u, 0.0f, 1.0f);
    out = a + r * tAB;
    return true;
}

bool HasUndirectedPlanarEdge(const VectorNetwork &network, uint32_t start, uint32_t end) {
    for (const VectorNetwork::Edge &edge : network.edges) {
        if ((edge.start == start && edge.end == end) || (edge.start == end && edge.end == start)) {
            return true;
        }
    }
    return false;
}

struct SampleChain {
    uint32_t startId = 0;
    uint32_t endId = 0;
    std::vector<Vec2> samples;
};

struct ChainSplit {
    float param = 0.0f;
    Vec2 point = {};
};

bool ChainSplitLess(const ChainSplit &left, const ChainSplit &right) {
    if (left.param != right.param) {
        return left.param < right.param;
    }
    if (left.point.x != right.point.x) {
        return left.point.x < right.point.x;
    }
    return left.point.y < right.point.y;
}

// Sample cubics, split at geometric crossings, emit a straight-edge planar
// network whose bounded faces match visually enclosed regions.
VectorNetwork BuildCurvePlanarNetwork(const VectorNetwork &source) {
    VectorNetwork planar;
    planar.vertices = source.vertices;

    const std::vector<size_t> activeEdges = UniqueUndirectedEdgeIndices(source);
    std::vector<SampleChain> chains;
    chains.reserve(activeEdges.size());
    for (size_t edgeIndex : activeEdges) {
        const VectorNetwork::Edge &edge = source.edges[edgeIndex];
        const VectorNetwork::Vertex *start = FindVertex(source, edge.start);
        const VectorNetwork::Vertex *end = FindVertex(source, edge.end);
        if (start == nullptr || end == nullptr) {
            continue;
        }
        SampleChain chain;
        chain.startId = edge.start;
        chain.endId = edge.end;
        const Vec2 p0 = start->point;
        const Vec2 p3 = end->point;
        const Vec2 c1 = p0 + edge.startTangent;
        const Vec2 c2 = p3 + edge.endTangent;
        chain.samples.reserve(static_cast<size_t>(kSamplesPerEdge) + 1);
        chain.samples.push_back(p0);
        for (int sample = 1; sample < kSamplesPerEdge; ++sample) {
            const float t = static_cast<float>(sample) / static_cast<float>(kSamplesPerEdge);
            chain.samples.push_back(EvalCubic(p0, c1, c2, p3, t));
        }
        chain.samples.push_back(p3);
        chains.push_back(std::move(chain));
    }

    std::vector<std::vector<ChainSplit>> splits(chains.size());
    for (size_t chainIndex = 0; chainIndex < chains.size(); ++chainIndex) {
        const SampleChain &chain = chains[chainIndex];
        for (size_t sampleIndex = 0; sampleIndex < chain.samples.size(); ++sampleIndex) {
            ChainSplit split;
            split.param = static_cast<float>(sampleIndex);
            split.point = chain.samples[sampleIndex];
            splits[chainIndex].push_back(split);
        }
    }

    for (size_t chainA = 0; chainA < chains.size(); ++chainA) {
        const SampleChain &aChain = chains[chainA];
        for (size_t segA = 0; segA + 1 < aChain.samples.size(); ++segA) {
            for (size_t chainB = chainA; chainB < chains.size(); ++chainB) {
                const SampleChain &bChain = chains[chainB];
                const size_t segBBegin = (chainB == chainA) ? segA + 2 : 0;
                for (size_t segB = segBBegin; segB + 1 < bChain.samples.size(); ++segB) {
                    Vec2 hit;
                    float tA = 0;
                    float tB = 0;
                    if (!IntersectSegments(aChain.samples[segA], aChain.samples[segA + 1],
                                           bChain.samples[segB], bChain.samples[segB + 1], hit, tA,
                                           tB)) {
                        continue;
                    }
                    // Endpoint hits are already sample splits.
                    if ((tA <= 1e-4f || tA >= 1.0f - 1e-4f) && (tB <= 1e-4f || tB >= 1.0f - 1e-4f)) {
                        continue;
                    }
                    splits[chainA].push_back({static_cast<float>(segA) + tA, hit});
                    splits[chainB].push_back({static_cast<float>(segB) + tB, hit});
                }
            }
        }
    }

    std::unordered_map<uint64_t, uint32_t> pointIds;
    pointIds.reserve(planar.vertices.size() * 2 + 8);
    auto ensurePoint = [&](Vec2 point, uint32_t preferredId) -> uint32_t {
        if (preferredId != 0) {
            VectorNetwork::Vertex *existing = FindVertex(planar, preferredId);
            if (existing != nullptr) {
                pointIds[QuantizePoint(existing->point)] = preferredId;
                return preferredId;
            }
        }
        const uint64_t key = QuantizePoint(point);
        const auto found = pointIds.find(key);
        if (found != pointIds.end()) {
            return found->second;
        }
        const uint32_t id = preferredId != 0 ? preferredId : AllocVertexId(planar);
        if (FindVertex(planar, id) == nullptr) {
            planar.vertices.push_back({id, point});
        }
        pointIds.emplace(key, id);
        return id;
    };

    for (const VectorNetwork::Vertex &vertex : planar.vertices) {
        pointIds[QuantizePoint(vertex.point)] = vertex.id;
    }

    for (size_t chainIndex = 0; chainIndex < chains.size(); ++chainIndex) {
        std::vector<ChainSplit> &chainSplits = splits[chainIndex];
        std::sort(chainSplits.begin(), chainSplits.end(), ChainSplitLess);
        std::vector<ChainSplit> unique;
        unique.reserve(chainSplits.size());
        for (const ChainSplit &split : chainSplits) {
            if (!unique.empty() &&
                (std::fabs(unique.back().param - split.param) <= 1e-4f ||
                 AlmostEqualPoint(unique.back().point, split.point))) {
                continue;
            }
            unique.push_back(split);
        }
        const SampleChain &chain = chains[chainIndex];
        for (size_t i = 0; i + 1 < unique.size(); ++i) {
            const bool atStart = i == 0;
            const bool atEnd = i + 2 == unique.size();
            const uint32_t startId =
                ensurePoint(unique[i].point, atStart ? chain.startId : 0);
            const uint32_t endId =
                ensurePoint(unique[i + 1].point, atEnd ? chain.endId : 0);
            if (startId == endId || HasUndirectedPlanarEdge(planar, startId, endId)) {
                continue;
            }
            planar.edges.push_back({AllocEdgeId(planar), startId, endId, {}, {}});
        }
    }
    return planar;
}

BezierPath BoundedFillFacesFromNetwork(const VectorNetwork &network) {
    BezierPath result;
    if (network.edges.empty() || network.vertices.empty()) {
        return result;
    }

    const std::vector<size_t> activeEdges = UniqueUndirectedEdgeIndices(network);
    if (activeEdges.empty()) {
        return result;
    }

    std::unordered_map<uint32_t, std::vector<Outgoing>> adjacency;
    for (size_t edgeIndex : activeEdges) {
        const VectorNetwork::Edge &edge = network.edges[edgeIndex];
        HalfEdge forward;
        forward.edgeIndex = edgeIndex;
        forward.forward = true;
        HalfEdge reverse = Twin(forward);

        Outgoing outForward;
        outForward.half = forward;
        outForward.angle = AngleOf(OutgoingDirection(network, forward));
        adjacency[edge.start].push_back(outForward);

        Outgoing outReverse;
        outReverse.half = reverse;
        outReverse.angle = AngleOf(OutgoingDirection(network, reverse));
        adjacency[edge.end].push_back(outReverse);
    }

    for (auto &entry : adjacency) {
        std::sort(entry.second.begin(), entry.second.end(), OutgoingAngleLess);
    }

    std::vector<uint8_t> used(network.edges.size() * 2, 0);
    std::vector<FaceWalk> faces;
    for (size_t edgeIndex : activeEdges) {
        for (int direction = 0; direction < 2; ++direction) {
            HalfEdge half;
            half.edgeIndex = edgeIndex;
            half.forward = direction == 0;
            if (IsUsed(used, half)) {
                continue;
            }
            FaceWalk face = WalkFace(network, adjacency, used, half);
            if (face.halves.size() < 3) {
                continue;
            }
            if (std::fabs(face.signedArea) <= 1e-6f) {
                continue;
            }
            faces.push_back(std::move(face));
        }
    }

    if (faces.empty()) {
        return result;
    }

    const std::vector<uint8_t> isOuter = OuterFaceMask(network, activeEdges, faces);
    for (size_t i = 0; i < faces.size(); ++i) {
        if (isOuter[i] != 0) {
            continue;
        }
        BezierPath::Contour contour = ContourFromFace(network, faces[i]);
        if (contour.vertices.size() >= 3) {
            result.contours.push_back(std::move(contour));
        }
    }
    return result;
}

}  // namespace

BezierPath CompileFillFaces(const VectorNetwork &network) {
    // Visual enclosure: subdivide cubics, split at crossings, then extract
    // bounded faces. Stroke still uses the original cubic edges.
    return BoundedFillFacesFromNetwork(BuildCurvePlanarNetwork(network));
}

BezierPath CompileStrokeEdges(const VectorNetwork &network) {
    BezierPath result;
    result.contours.reserve(network.edges.size());
    for (const VectorNetwork::Edge &edge : network.edges) {
        if (edge.start == edge.end) {
            continue;
        }
        const VectorNetwork::Vertex *start = FindVertex(network, edge.start);
        const VectorNetwork::Vertex *end = FindVertex(network, edge.end);
        if (start == nullptr || end == nullptr) {
            continue;
        }
        BezierPath::Contour contour;
        contour.closed = false;
        BezierPath::Vertex from;
        from.point = start->point;
        from.outTangent = edge.startTangent;
        BezierPath::Vertex to;
        to.point = end->point;
        to.inTangent = edge.endTangent;
        contour.vertices.push_back(from);
        contour.vertices.push_back(to);
        result.contours.push_back(std::move(contour));
    }
    return result;
}

}  // namespace motion
