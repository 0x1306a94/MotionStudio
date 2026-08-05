#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/common/VectorNetwork.h"
#include "MotionStudio/render/DrawCommand.h"
#include "MotionStudio/render/SceneState.h"

namespace motion {

// What path is being edited: a shape geometry or a layer mask.
enum class PathEditKind {
    Shape = 0,
    Mask,
};

// Identifies the editable path on a layer.
struct PathEditTarget {
    PathEditKind kind = PathEditKind::Shape;
    EntityId layerId;
    // Mask list index when kind is Mask; unused for Shape.
    int maskIndex = 0;
};

// Hit target on path-edit chrome. Pick priority: EdgeTangent / In/OutTangent >
// Vertex > CloseRing > Segment(Edge).
enum class PathHandleKind {
    None = 0,
    Vertex,
    InTangent,
    OutTangent,
    Segment,
    CloseRing,
    // Handle on a specific edge endpoint (used when vertex degree != 2).
    EdgeTangent,
};

// Result of HitTestPathEdit.
// - Vertex / CloseRing: index into network.vertices; vertexId set
// - InTangent / OutTangent: index = selected vertex; edgeId + atStart set
// - EdgeTangent: edgeId + atStart; index = selected vertex; vertexId set
// - Segment: edgeId identifies the edge; index = edge list index; segmentT in [0,1]
struct PathEditHit {
    PathHandleKind kind = PathHandleKind::None;
    size_t index = 0;
    float segmentT = 0;
    uint32_t vertexId = 0;
    uint32_t edgeId = 0;
    bool atStart = true;
};

// World-space caches for path vertex / tangent chrome.
struct PathEditHandles {
    bool valid = false;
    PathEditTarget target;
    // Authoring network (preferred).
    VectorNetwork localNetwork;
    // Stroke overlay path (CompileStrokeEdges) for yellow chrome.
    BezierPath localPath;
    Mat3 worldTransform = Mat3::Identity();
    // -1 when no vertex is selected (tangent handles hidden).
    // Index into localNetwork.vertices / worldVertices.
    int selectedVertex = -1;
    std::vector<Vec2> worldVertices;
    // Parallel to vertices; meaningful for degree==2 selected vertex.
    std::vector<Vec2> worldInHandles;
    std::vector<Vec2> worldOutHandles;
};

// Resolves the target path from state and builds world caches. Returns false
// when the layer/path is missing or empty.
bool BuildPathEditHandles(const SceneState &state, PathEditTarget target, int selectedVertex,
                          PathEditHandles &out);

// Builds handles from an authoring network.
bool BuildPathEditHandlesFromNetwork(const VectorNetwork &network, Mat3 worldTransform,
                                     PathEditTarget target, int selectedVertex,
                                     PathEditHandles &out);

// Builds handles from an explicit local path (draft / preview / tests).
// Converts to a single-ring network internally.
bool BuildPathEditHandlesFromPath(const BezierPath &localPath, Mat3 worldTransform,
                                  PathEditTarget target, int selectedVertex,
                                  PathEditHandles &out);

// Picks the topmost path-edit handle under scenePoint.
// handleHitRadius: vertex / tangent hit radius in scene units.
// segmentHitRadius: stroke hit radius for Segment / path edge.
PathEditHit HitTestPathEdit(const PathEditHandles &handles, Vec2 scenePoint,
                            float handleHitRadius, float segmentHitRadius);

// Draws path stroke, vertex markers, and (for selectedVertex) tangent chrome.
// strokeWidth / handleSize are in scene units.
DrawCommandList BuildPathEditCommands(const PathEditHandles &handles, float strokeWidth,
                                      float handleSize);

}  // namespace motion
