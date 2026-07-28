#include "motionstudio_bridge.h"

#include <cstdlib>

#include "MotionStudio/common/PathGeometryEdit.h"
#include "MotionStudio/common/Vec2.h"

#include "BridgeInternals.h"
#include "DocumentLock.h"
#include "MSDocument.h"

using namespace bridge;

using motion::Vec2;


void ms_bezier_path_free(MSBezierPath *path) {
    if (path == nullptr) {
        return;
    }
    free(path->vertices);
    free(path);
}

MSBezierPath *ms_bezier_move_vertex(const MSBezierPath *path, size_t index, float x, float y,
                                    bool linkedHandles) {
    if (path == nullptr) {
        return nullptr;
    }
    return AllocateMSBezierPath(
        motion::MoveVertex(FromMSBezierPath(path), index, Vec2{x, y}, linkedHandles));
}

MSBezierPath *ms_bezier_move_in_tangent(const MSBezierPath *path, size_t index, float x, float y,
                                        bool mirrorOut) {
    if (path == nullptr) {
        return nullptr;
    }
    return AllocateMSBezierPath(
        motion::MoveInTangent(FromMSBezierPath(path), index, Vec2{x, y}, mirrorOut));
}

MSBezierPath *ms_bezier_move_out_tangent(const MSBezierPath *path, size_t index, float x, float y,
                                         bool mirrorIn) {
    if (path == nullptr) {
        return nullptr;
    }
    return AllocateMSBezierPath(
        motion::MoveOutTangent(FromMSBezierPath(path), index, Vec2{x, y}, mirrorIn));
}

MSBezierPath *ms_bezier_insert_vertex_on_segment(const MSBezierPath *path, size_t segmentIndex,
                                                 float t) {
    if (path == nullptr) {
        return nullptr;
    }
    return AllocateMSBezierPath(
        motion::InsertVertexOnSegment(FromMSBezierPath(path), segmentIndex, t));
}

MSBezierPath *ms_bezier_remove_vertex(const MSBezierPath *path, size_t index) {
    if (path == nullptr) {
        return nullptr;
    }
    return AllocateMSBezierPath(motion::RemoveVertex(FromMSBezierPath(path), index));
}

MSBezierPath *ms_bezier_close_path(const MSBezierPath *path) {
    if (path == nullptr) {
        return nullptr;
    }
    return AllocateMSBezierPath(motion::ClosePath(FromMSBezierPath(path)));
}

MSBezierPath *ms_bezier_append_vertex(const MSBezierPath *path, float x, float y) {
    motion::BezierPath::Vertex vertex;
    vertex.point = {x, y};
    return AllocateMSBezierPath(motion::AppendVertex(FromMSBezierPath(path), vertex));
}

MSBezierPath *ms_bezier_toggle_vertex_smooth(const MSBezierPath *path, size_t index) {
    if (path == nullptr) {
        return nullptr;
    }
    return AllocateMSBezierPath(motion::ToggleVertexSmooth(FromMSBezierPath(path), index));
}
