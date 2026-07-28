#pragma once

#include <cstddef>
#include <vector>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/Vec2.h"
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

// Hit target on path-edit chrome. Pick priority: In/OutTangent > Vertex >
// CloseRing > Segment.
enum class PathHandleKind {
    None = 0,
    Vertex,
    InTangent,
    OutTangent,
    Segment,
    CloseRing,
};

// Result of HitTestPathEdit. index is a vertex index for Vertex / InTangent /
// OutTangent / CloseRing, or a segment index for Segment. segmentT is set for
// Segment hits (clamped to [0, 1]).
struct PathEditHit {
    PathHandleKind kind = PathHandleKind::None;
    size_t index = 0;
    float segmentT = 0;
};

// World-space caches for path vertex / tangent chrome.
struct PathEditHandles {
    bool valid = false;
    PathEditTarget target;
    BezierPath localPath;
    Mat3 worldTransform = Mat3::Identity();
    // -1 when no vertex is selected (tangent handles hidden).
    int selectedVertex = -1;
    std::vector<Vec2> worldVertices;
    std::vector<Vec2> worldInHandles;
    std::vector<Vec2> worldOutHandles;
};

// Resolves the target path from state and builds world caches. Returns false
// when the layer/path is missing or empty.
bool BuildPathEditHandles(const SceneState &state, PathEditTarget target, int selectedVertex,
                          PathEditHandles &out);

// Builds handles from an explicit local path (draft / preview). Returns false
// when localPath has no vertices.
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
