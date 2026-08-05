#include "MotionStudio/render/PathEditHandles.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "MotionStudio/common/VectorNetworkCompile.h"
#include "MotionStudio/common/VectorNetworkConvert.h"
#include "MotionStudio/render/PathOverlay.h"
#include "MotionStudio/render/ShapeGeometry.h"

namespace motion {

namespace {

constexpr Color kPathEditStrokeColor{1.0f, 0.85f, 0.2f, 1.0f};
constexpr Color kHandleFillColor{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color kSelectedHandleFillColor{1.0f, 0.85f, 0.2f, 1.0f};
constexpr Color kTangentFillColor{0.35f, 0.75f, 1.0f, 1.0f};
// Distinct from vertex chrome yellow so tangent stems stay visible.
constexpr Color kTangentStrokeColor{0.25f, 0.55f, 0.95f, 1.0f};
constexpr int kSegmentSamples = 16;

float LengthSquared(Vec2 value) {
    return value.x * value.x + value.y * value.y;
}

bool IsNearPoint(Vec2 point, Vec2 target, float radius) {
    return LengthSquared(point - target) <= radius * radius;
}

Vec2 CubicPoint(Vec2 p0, Vec2 c1, Vec2 c2, Vec2 p3, float t) {
    const float mt = 1.0f - t;
    return p0 * (mt * mt * mt) + c1 * (3.0f * mt * mt * t) + c2 * (3.0f * mt * t * t) +
        p3 * (t * t * t);
}

const EvaluatedLayer *FindLayer(const SceneState &state, EntityId id) {
    for (const EvaluatedLayer &layer : state.layers) {
        if (layer.id == id) {
            return &layer;
        }
    }
    return nullptr;
}

bool ResolveLocalNetwork(const EvaluatedLayer &layer, PathEditTarget target,
                         VectorNetwork &outNetwork) {
    if (target.kind == PathEditKind::Mask) {
        if (target.maskIndex < 0 ||
            static_cast<size_t>(target.maskIndex) >= layer.masks.size()) {
            return false;
        }
        const EvaluatedMask &mask = layer.masks[static_cast<size_t>(target.maskIndex)];
        if (!mask.network.vertices.empty()) {
            outNetwork = mask.network;
            return true;
        }
        outNetwork = BezierPathToVectorNetwork(mask.path);
        return !outNetwork.vertices.empty();
    }
    if (!layer.shapeNetwork.vertices.empty()) {
        outNetwork = layer.shapeNetwork;
        return true;
    }
    if (layer.shapeItems.empty()) {
        return false;
    }
    outNetwork =
        BezierPathToVectorNetwork(ShapeGeometryToBezierPath(layer.shapeItems.front().geometry));
    return !outNetwork.vertices.empty();
}

void AppendStroke(DrawCommandList &commands, ShapeGeometry geometry, float strokeWidth,
                  Color color) {
    DrawCommand command;
    command.type = DrawCommandType::StrokePath;
    command.geometry = std::move(geometry);
    command.paint = Paint{color, FillRule::NonZero};
    command.stroke.width = strokeWidth;
    command.stroke.join = LineJoin::Miter;
    commands.push_back(std::move(command));
}

void AppendFill(DrawCommandList &commands, ShapeGeometry geometry, Color color) {
    DrawCommand command;
    command.type = DrawCommandType::DrawPath;
    command.geometry = std::move(geometry);
    command.paint = Paint{color, FillRule::NonZero};
    commands.push_back(std::move(command));
}

ShapeGeometry AxisAlignedSquare(Vec2 center, float size) {
    return MakeRectGeometry(center, {size, size});
}

void AppendTangentChrome(DrawCommandList &commands, Vec2 vertex, Vec2 handle, float strokeWidth,
                         float handleSize) {
    if (IsNearPoint(handle, vertex, 1e-4f)) {
        return;
    }
    BezierPath line = MakeSingleContour({{vertex, {}, {}}, {handle, {}, {}}}, false);
    AppendStroke(commands, MakePathGeometry(std::move(line)), strokeWidth, kTangentStrokeColor);
    const float tangentSize = handleSize * 0.85f;
    AppendFill(commands, MakeEllipseGeometry(handle, {tangentSize, tangentSize}),
               kTangentFillColor);
    AppendStroke(commands, MakeEllipseGeometry(handle, {tangentSize, tangentSize}), strokeWidth,
                 kTangentStrokeColor);
}

bool ClosestOnEdge(const VectorNetwork &network, const VectorNetwork::Edge &edge, Vec2 localPoint,
                   float &outT, Vec2 &outClosestLocal) {
    const VectorNetwork::Vertex *start = FindVertex(network, edge.start);
    const VectorNetwork::Vertex *end = FindVertex(network, edge.end);
    if (start == nullptr || end == nullptr) {
        outT = 0;
        outClosestLocal = {};
        return false;
    }
    const Vec2 p0 = start->point;
    const Vec2 c1 = start->point + edge.startTangent;
    const Vec2 c2 = end->point + edge.endTangent;
    const Vec2 p3 = end->point;

    float bestDistance = LengthSquared(localPoint - p0);
    float bestT = 0.0f;
    Vec2 bestPoint = p0;
    for (int sample = 1; sample <= kSegmentSamples; ++sample) {
        const float t = static_cast<float>(sample) / static_cast<float>(kSegmentSamples);
        const Vec2 point = CubicPoint(p0, c1, c2, p3, t);
        const float distance = LengthSquared(localPoint - point);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestT = t;
            bestPoint = point;
        }
    }
    outT = bestT;
    outClosestLocal = bestPoint;
    return true;
}

// For degree==2: incoming edge (vertex is end) supplies in-handle; outgoing
// (vertex is start) supplies out-handle. Matches single-ring Bezier semantics.
void ResolveDegreeTwoHandles(const VectorNetwork &network, uint32_t vertexId, Vec2 point,
                             Vec2 &outInLocal, Vec2 &outOutLocal, uint32_t &outInEdgeId,
                             uint32_t &outOutEdgeId) {
    outInLocal = point;
    outOutLocal = point;
    outInEdgeId = 0;
    outOutEdgeId = 0;
    for (const VectorNetwork::Edge &edge : network.edges) {
        if (edge.end == vertexId && outInEdgeId == 0) {
            outInLocal = point + edge.endTangent;
            outInEdgeId = edge.id;
        }
        if (edge.start == vertexId && outOutEdgeId == 0) {
            outOutLocal = point + edge.startTangent;
            outOutEdgeId = edge.id;
        }
    }
}

void CollectIncidentEdgeHandles(const VectorNetwork &network, uint32_t vertexId, Vec2 point,
                                Mat3 worldTransform, std::vector<Vec2> &outHandles,
                                std::vector<uint32_t> &outEdgeIds, std::vector<bool> &outAtStart) {
    outHandles.clear();
    outEdgeIds.clear();
    outAtStart.clear();
    for (const VectorNetwork::Edge &edge : network.edges) {
        if (edge.start == vertexId) {
            const Vec2 local = point + edge.startTangent;
            if (!IsNearPoint(local, point, 1e-4f)) {
                outHandles.push_back(worldTransform.transformPoint(local));
                outEdgeIds.push_back(edge.id);
                outAtStart.push_back(true);
            }
        }
        if (edge.end == vertexId) {
            const Vec2 local = point + edge.endTangent;
            if (!IsNearPoint(local, point, 1e-4f)) {
                outHandles.push_back(worldTransform.transformPoint(local));
                outEdgeIds.push_back(edge.id);
                outAtStart.push_back(false);
            }
        }
    }
}

}  // namespace

bool BuildPathEditHandlesFromNetwork(const VectorNetwork &network, Mat3 worldTransform,
                                     PathEditTarget target, int selectedVertex,
                                     PathEditHandles &out) {
    out = {};
    if (network.vertices.empty()) {
        return false;
    }
    out.valid = true;
    out.target = target;
    out.localNetwork = network;
    out.localPath = CompileStrokeEdges(network);
    out.worldTransform = worldTransform;
    out.selectedVertex = selectedVertex;
    out.worldVertices.reserve(network.vertices.size());
    out.worldInHandles.reserve(network.vertices.size());
    out.worldOutHandles.reserve(network.vertices.size());

    for (const VectorNetwork::Vertex &vertex : network.vertices) {
        const Vec2 worldPoint = worldTransform.transformPoint(vertex.point);
        out.worldVertices.push_back(worldPoint);
        out.worldInHandles.push_back(worldPoint);
        out.worldOutHandles.push_back(worldPoint);
    }

    if (selectedVertex < 0 || static_cast<size_t>(selectedVertex) >= network.vertices.size()) {
        out.selectedVertex = -1;
        return true;
    }

    const size_t selected = static_cast<size_t>(selectedVertex);
    const VectorNetwork::Vertex &vertex = network.vertices[selected];
    if (VertexDegree(network, vertex.id) == 2) {
        Vec2 inLocal;
        Vec2 outLocal;
        uint32_t inEdgeId = 0;
        uint32_t outEdgeId = 0;
        ResolveDegreeTwoHandles(network, vertex.id, vertex.point, inLocal, outLocal, inEdgeId,
                                outEdgeId);
        out.worldInHandles[selected] = worldTransform.transformPoint(inLocal);
        out.worldOutHandles[selected] = worldTransform.transformPoint(outLocal);
    }
    return true;
}

bool BuildPathEditHandlesFromPath(const BezierPath &localPath, Mat3 worldTransform,
                                  PathEditTarget target, int selectedVertex,
                                  PathEditHandles &out) {
    return BuildPathEditHandlesFromNetwork(BezierPathToVectorNetwork(localPath), worldTransform,
                                           target, selectedVertex, out);
}

bool BuildPathEditHandles(const SceneState &state, PathEditTarget target, int selectedVertex,
                          PathEditHandles &out) {
    out = {};
    if (!target.layerId.isValid()) {
        return false;
    }
    const EvaluatedLayer *layer = FindLayer(state, target.layerId);
    if (layer == nullptr) {
        return false;
    }
    VectorNetwork network;
    if (!ResolveLocalNetwork(*layer, target, network)) {
        return false;
    }
    return BuildPathEditHandlesFromNetwork(network, layer->worldTransform, target, selectedVertex,
                                           out);
}

PathEditHit HitTestPathEdit(const PathEditHandles &handles, Vec2 scenePoint,
                            float handleHitRadius, float segmentHitRadius) {
    PathEditHit hit;
    if (!handles.valid) {
        return hit;
    }
    const float handleRadius = std::max(handleHitRadius, 0.0f);
    const float segmentRadius = std::max(segmentHitRadius, 0.0f);
    const float handleRadiusSquared = handleRadius * handleRadius;
    const float segmentRadiusSquared = segmentRadius * segmentRadius;
    const VectorNetwork &network = handles.localNetwork;

    if (handles.selectedVertex >= 0) {
        const size_t selected = static_cast<size_t>(handles.selectedVertex);
        const VectorNetwork::Vertex &vertex = network.vertices[selected];
        const int degree = VertexDegree(network, vertex.id);
        if (degree == 2) {
            const Vec2 worldVertex = handles.worldVertices[selected];
            if (!IsNearPoint(handles.worldInHandles[selected], worldVertex, 1e-4f) &&
                IsNearPoint(scenePoint, handles.worldInHandles[selected], handleRadius)) {
                Vec2 inLocal;
                Vec2 outLocal;
                uint32_t inEdgeId = 0;
                uint32_t outEdgeId = 0;
                ResolveDegreeTwoHandles(network, vertex.id, vertex.point, inLocal, outLocal,
                                        inEdgeId, outEdgeId);
                hit.kind = PathHandleKind::InTangent;
                hit.index = selected;
                hit.vertexId = vertex.id;
                hit.edgeId = inEdgeId;
                hit.atStart = false;
                return hit;
            }
            if (!IsNearPoint(handles.worldOutHandles[selected], worldVertex, 1e-4f) &&
                IsNearPoint(scenePoint, handles.worldOutHandles[selected], handleRadius)) {
                Vec2 inLocal;
                Vec2 outLocal;
                uint32_t inEdgeId = 0;
                uint32_t outEdgeId = 0;
                ResolveDegreeTwoHandles(network, vertex.id, vertex.point, inLocal, outLocal,
                                        inEdgeId, outEdgeId);
                hit.kind = PathHandleKind::OutTangent;
                hit.index = selected;
                hit.vertexId = vertex.id;
                hit.edgeId = outEdgeId;
                hit.atStart = true;
                return hit;
            }
        } else {
            std::vector<Vec2> edgeHandles;
            std::vector<uint32_t> edgeIds;
            std::vector<bool> atStarts;
            CollectIncidentEdgeHandles(network, vertex.id, vertex.point, handles.worldTransform,
                                       edgeHandles, edgeIds, atStarts);
            for (size_t i = 0; i < edgeHandles.size(); ++i) {
                if (IsNearPoint(scenePoint, edgeHandles[i], handleRadius)) {
                    hit.kind = PathHandleKind::EdgeTangent;
                    hit.index = selected;
                    hit.vertexId = vertex.id;
                    hit.edgeId = edgeIds[i];
                    hit.atStart = atStarts[i];
                    return hit;
                }
            }
        }
    }

    // Vertices own an exclusive hit zone so clicks on a vertex never become
    // Insert-on-segment / Append. Mid-edge insert uses the remaining space.
    float bestVertexDistanceSquared = handleRadiusSquared;
    int bestVertex = -1;
    for (size_t index = 0; index < handles.worldVertices.size(); ++index) {
        const float distanceSquared = LengthSquared(scenePoint - handles.worldVertices[index]);
        if (distanceSquared <= bestVertexDistanceSquared) {
            bestVertexDistanceSquared = distanceSquared;
            bestVertex = static_cast<int>(index);
        }
    }
    if (bestVertex >= 0) {
        const size_t index = static_cast<size_t>(bestVertex);
        const BezierPath ring = VectorNetworkToSingleRingBezierPath(network);
        const BezierPath::Contour *hitContour = PrimaryContour(ring);
        if (hitContour != nullptr && !hitContour->closed && index == 0 &&
            handles.worldVertices.size() >= 2) {
            hit.kind = PathHandleKind::CloseRing;
        } else {
            hit.kind = PathHandleKind::Vertex;
        }
        hit.index = index;
        if (index < network.vertices.size()) {
            hit.vertexId = network.vertices[index].id;
        }
        return hit;
    }

    Mat3 inverse;
    if (!handles.worldTransform.tryInvert(inverse)) {
        return hit;
    }
    const Vec2 localPoint = inverse.transformPoint(scenePoint);
    float bestSegmentDistanceSquared = segmentRadiusSquared;
    int bestEdgeIndex = -1;
    float bestSegmentT = 0;
    uint32_t bestEdgeId = 0;
    for (size_t edgeIndex = 0; edgeIndex < network.edges.size(); ++edgeIndex) {
        const VectorNetwork::Edge &edge = network.edges[edgeIndex];
        float t = 0;
        Vec2 closestLocal;
        if (!ClosestOnEdge(network, edge, localPoint, t, closestLocal)) {
            continue;
        }
        const Vec2 closestWorld = handles.worldTransform.transformPoint(closestLocal);
        const float distanceSquared = LengthSquared(scenePoint - closestWorld);
        if (distanceSquared <= bestSegmentDistanceSquared) {
            bestSegmentDistanceSquared = distanceSquared;
            bestEdgeIndex = static_cast<int>(edgeIndex);
            bestSegmentT = t;
            bestEdgeId = edge.id;
        }
    }
    if (bestEdgeIndex < 0) {
        return hit;
    }
    hit.kind = PathHandleKind::Segment;
    hit.index = static_cast<size_t>(bestEdgeIndex);
    hit.segmentT = bestSegmentT;
    hit.edgeId = bestEdgeId;
    return hit;
}

DrawCommandList BuildPathEditCommands(const PathEditHandles &handles, float strokeWidth,
                                      float handleSize) {
    DrawCommandList commands;
    if (!handles.valid) {
        return commands;
    }
    const float safeStroke = std::max(strokeWidth, 0.0f);
    const float safeHandle = std::max(handleSize, safeStroke);
    const VectorNetwork &network = handles.localNetwork;

    PathOverlayItem overlay;
    overlay.worldTransform = handles.worldTransform;
    overlay.path = handles.localPath;
    overlay.color = kPathEditStrokeColor;
    DrawCommandList strokeCommands = BuildPathOverlayCommands({overlay}, safeStroke);
    commands.insert(commands.end(), strokeCommands.begin(), strokeCommands.end());

    if (handles.selectedVertex >= 0) {
        const size_t selected = static_cast<size_t>(handles.selectedVertex);
        const VectorNetwork::Vertex &vertex = network.vertices[selected];
        const Vec2 worldVertex = handles.worldVertices[selected];
        if (VertexDegree(network, vertex.id) == 2) {
            AppendTangentChrome(commands, worldVertex, handles.worldInHandles[selected], safeStroke,
                                safeHandle);
            AppendTangentChrome(commands, worldVertex, handles.worldOutHandles[selected],
                                safeStroke, safeHandle);
        } else {
            std::vector<Vec2> edgeHandles;
            std::vector<uint32_t> edgeIds;
            std::vector<bool> atStarts;
            CollectIncidentEdgeHandles(network, vertex.id, vertex.point, handles.worldTransform,
                                       edgeHandles, edgeIds, atStarts);
            for (const Vec2 &handle : edgeHandles) {
                AppendTangentChrome(commands, worldVertex, handle, safeStroke, safeHandle);
            }
        }
    }

    for (size_t index = 0; index < handles.worldVertices.size(); ++index) {
        const Vec2 &vertex = handles.worldVertices[index];
        const bool selected =
            handles.selectedVertex >= 0 && static_cast<size_t>(handles.selectedVertex) == index;
        AppendFill(commands, AxisAlignedSquare(vertex, safeHandle),
                   selected ? kSelectedHandleFillColor : kHandleFillColor);
        AppendStroke(commands, AxisAlignedSquare(vertex, safeHandle), safeStroke,
                     kPathEditStrokeColor);
    }
    return commands;
}

}  // namespace motion
