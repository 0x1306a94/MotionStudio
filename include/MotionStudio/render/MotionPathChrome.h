#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/render/DrawCommand.h"

namespace motion {

class Document;

// Hit target on motion-path chrome. Priority when a keyframe is selected:
// InTangent / OutTangent > Keyframe.
enum class MotionPathHandleKind {
    None = 0,
    Keyframe,
    InTangent,
    OutTangent,
};

struct MotionPathHit {
    MotionPathHandleKind kind = MotionPathHandleKind::None;
    size_t index = 0;
};

// One SetSpatialTangents write produced by a handle drag.
struct MotionPathSpatialUpdate {
    FrameTime time = 0;
    std::optional<Vec2> spatialIn;
    std::optional<Vec2> spatialOut;
};

// Preview chrome for a layer's transform.position motion path (parent space).
struct MotionPathChrome {
    bool valid = false;
    EntityId layerId;
    BezierPath path;
    Mat3 parentWorldTransform = Mat3::Identity();
    int selectedKeyframe = -1;
    std::vector<FrameTime> keyframeTimes;
    std::vector<Vec2> localVertices;
    // Display tangents (stored value, or segment/3 preview when unset).
    std::vector<Vec2> displayInTangents;
    std::vector<Vec2> displayOutTangents;
    std::vector<bool> hasStoredIn;
    std::vector<bool> hasStoredOut;
    std::vector<Vec2> worldVertices;
    std::vector<Vec2> worldInHandles;
    std::vector<Vec2> worldOutHandles;
};

// Builds chrome for layerId when transform.position has ≥2 keyframes.
// selectedKeyframe: index into ascending keyframe list, or -1.
bool BuildMotionPathChrome(const Document &document, EntityId layerId, PreviewTime time,
                           int selectedKeyframe, MotionPathChrome &out);

// Picks the topmost motion-path handle under scenePoint (scene units).
MotionPathHit HitTestMotionPath(const MotionPathChrome &chrome, Vec2 scenePoint,
                                float handleHitRadius);

// Draws path stroke, keyframe markers, and (for selectedKeyframe) tangent chrome
// using display tangents so unset handles remain pullable.
DrawCommandList BuildMotionPathCommands(const MotionPathChrome &chrome, float strokeWidth,
                                        float handleSize);

// Converts a scene-space handle drag into one or two spatial writes (this KF, and
// a default opposite end on the adjacent KF when that end is still unset).
std::vector<MotionPathSpatialUpdate> MotionPathTangentDragUpdates(
    const Document &document, EntityId layerId, size_t keyframeIndex, bool isOut,
    Vec2 scenePoint, Mat3 parentWorldTransform);

}  // namespace motion
