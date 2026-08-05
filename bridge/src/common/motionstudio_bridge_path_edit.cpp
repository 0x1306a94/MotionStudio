#include "motionstudio_bridge.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/PathGeometryEdit.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/common/VectorNetworkEdit.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/render/SceneEvaluator.h"
#include "MotionStudio/undo/AddKeyframeCommand.h"
#include "MotionStudio/undo/SetStaticValueCommand.h"
#include "MotionStudio/undo/UndoManager.h"

#include "BridgeInternals.h"
#include "DocumentLock.h"
#include "MSDocument.h"

using namespace bridge;

using motion::Animatable;
using motion::AnimatableBase;
using motion::AnimatableType;
using motion::Document;
using motion::EntityId;
using motion::FrameTime;
using motion::SceneEvaluator;
using motion::Vec2;

namespace {

std::string PathEditPropertyPath(MS_PATH_EDIT kind, int maskIndex) {
    if (kind == MS_PATH_EDIT_MASK) {
        return "masks[" + std::to_string(maskIndex) + "].path";
    }
    return "path";
}

EntityId CompositionIdForLayer(Document &document, EntityId layerId) {
    for (const auto &composition : document.compositions) {
        for (const auto &layer : composition->layers) {
            if (layer->id == layerId) {
                return composition->id;
            }
        }
    }
    return {};
}

bool ScenePointToLocal(Document &document, EntityId layerId, FrameTime frame, Vec2 scenePoint,
                       Vec2 &localOut) {
    const EntityId compositionId = CompositionIdForLayer(document, layerId);
    if (!compositionId.isValid()) {
        return false;
    }
    auto result = SceneEvaluator::Evaluate(document, compositionId, frame);
    if (!result.hasValue()) {
        return false;
    }
    for (const motion::EvaluatedLayer &layer : result.value().layers) {
        if (layer.id != layerId) {
            continue;
        }
        motion::Mat3 inverse;
        if (!layer.worldTransform.tryInvert(inverse)) {
            return false;
        }
        localOut = inverse.transformPoint(scenePoint);
        return true;
    }
    return false;
}

void WriteBezierPathUnlocked(MSDocument *document, uint64_t entityId, const char *path,
                             FrameTime frame, const motion::BezierPath &value) {
    AnimatableBase *property = FindProperty(document, entityId, path);
    if (property == nullptr || property->valueType() != AnimatableType::VectorNetwork) {
        return;
    }
    const motion::VectorNetwork network = BridgeNetworkFromPath(value);
    if (static_cast<Animatable<motion::VectorNetwork> *>(property)->isAnimated()) {
        Execute(document,
                std::make_unique<motion::AddKeyframeCommand>(
                    MakePath(entityId, path),
                    motion::KeyframeData(MakeKeyframe(frame, network))));
        return;
    }
    Execute(document, std::make_unique<motion::SetStaticValueCommand>(MakePath(entityId, path), motion::PropertyValue(network)));
}

void WriteNetworkUnlocked(MSDocument *document, uint64_t entityId, const char *path, FrameTime frame,
                          const motion::VectorNetwork &network) {
    AnimatableBase *property = FindProperty(document, entityId, path);
    if (property == nullptr || property->valueType() != AnimatableType::VectorNetwork) {
        return;
    }
    if (static_cast<Animatable<motion::VectorNetwork> *>(property)->isAnimated()) {
        Execute(document,
                std::make_unique<motion::AddKeyframeCommand>(
                    MakePath(entityId, path),
                    motion::KeyframeData(MakeKeyframe(frame, network))));
        return;
    }
    Execute(document, std::make_unique<motion::SetStaticValueCommand>(MakePath(entityId, path), motion::PropertyValue(network)));
}

void WriteVec2AtPlayheadUnlocked(MSDocument *document, uint64_t entityId, const char *path,
                                 FrameTime frame, Vec2 value) {
    AnimatableBase *property = FindProperty(document, entityId, path);
    if (property == nullptr || property->valueType() != AnimatableType::Vec2) {
        return;
    }
    if (static_cast<Animatable<Vec2> *>(property)->isAnimated()) {
        Execute(document,
                std::make_unique<motion::AddKeyframeCommand>(
                    MakePath(entityId, path),
                    motion::KeyframeData(MakeKeyframe(frame, value))));
        return;
    }
    Execute(document, std::make_unique<motion::SetStaticValueCommand>(MakePath(entityId, path), motion::PropertyValue(value)));
}

motion::BezierPath CurrentBezierPath(MSDocument *document, uint64_t entityId, const char *path,
                                     FrameTime frame) {
    const Animatable<motion::VectorNetwork> *property =
        AsVectorNetwork(FindProperty(document, entityId, path));
    if (property == nullptr) {
        return {};
    }
    return BridgePathFromNetwork(property->evaluate(frame));
}

motion::VectorNetwork CurrentNetwork(MSDocument *document, uint64_t entityId, const char *path,
                                     FrameTime frame) {
    const Animatable<motion::VectorNetwork> *property =
        AsVectorNetwork(FindProperty(document, entityId, path));
    if (property == nullptr) {
        return {};
    }
    return property->evaluate(frame);
}

bool LayerWorldTransform(Document &document, EntityId layerId, FrameTime frame, motion::Mat3 &out) {
    const EntityId compositionId = CompositionIdForLayer(document, layerId);
    if (!compositionId.isValid()) {
        return false;
    }
    auto result = SceneEvaluator::Evaluate(document, compositionId, frame);
    if (!result.hasValue()) {
        return false;
    }
    for (const motion::EvaluatedLayer &layer : result.value().layers) {
        if (layer.id != layerId) {
            continue;
        }
        out = layer.worldTransform;
        return true;
    }
    return false;
}

// Rebases a Shape path so its bounds center is local (0,0) and bumps
// transform.position to keep the world silhouette unchanged.
void RecenterShapePathUnlocked(MSDocument *document, uint64_t layerId, FrameTime frame) {
    motion::VectorNetwork network = CurrentNetwork(document, layerId, "path", frame);
    if (network.vertices.empty()) {
        return;
    }
    motion::Mat3 world = motion::Mat3::Identity();
    const bool hasWorld = LayerWorldTransform(*document->document, EntityId{layerId}, frame, world);
    Vec2 localCenter{};
    network = motion::RecenterNetwork(std::move(network), localCenter);
    if (localCenter.x == 0.0f && localCenter.y == 0.0f) {
        return;
    }
    const Animatable<Vec2> *positionProperty =
        AsVec2(FindProperty(document, layerId, "transform.position"));
    if (positionProperty == nullptr) {
        WriteNetworkUnlocked(document, layerId, "path", frame, network);
        return;
    }
    const Vec2 delta = hasWorld ? world.transformVector(localCenter) : localCenter;
    const Vec2 newPosition = positionProperty->evaluate(frame) + delta;
    document->undoManager->beginMergeGroup();
    WriteNetworkUnlocked(document, layerId, "path", frame, network);
    WriteVec2AtPlayheadUnlocked(document, layerId, "transform.position", frame, newPosition);
    document->undoManager->endMergeGroup();
}

}  // namespace

void ms_command_path_edit_move_vertex(MSDocument *document, uint64_t layerId, MS_PATH_EDIT kind,
                                      int maskIndex, int64_t frame, size_t index, float sceneX,
                                      float sceneY, bool linkedHandles) {
    DocumentLock guard(document);
    if (document == nullptr || kind == MS_PATH_EDIT_NONE) {
        return;
    }
    const std::string propertyPath = PathEditPropertyPath(kind, maskIndex);
    const FrameTime frameTime = static_cast<FrameTime>(frame);
    Vec2 localPoint;
    if (!ScenePointToLocal(*document->document, EntityId{layerId}, frameTime, {sceneX, sceneY},
                           localPoint)) {
        return;
    }
    motion::BezierPath edited =
        motion::MoveVertex(CurrentBezierPath(document, layerId, propertyPath.c_str(), frameTime),
                           index, localPoint, linkedHandles);
    WriteBezierPathUnlocked(document, layerId, propertyPath.c_str(), frameTime, edited);
}

void ms_command_path_edit_move_in_tangent(MSDocument *document, uint64_t layerId, MS_PATH_EDIT kind,
                                          int maskIndex, int64_t frame, size_t index, float sceneX,
                                          float sceneY, bool mirrorOut) {
    DocumentLock guard(document);
    if (document == nullptr || kind == MS_PATH_EDIT_NONE) {
        return;
    }
    const std::string propertyPath = PathEditPropertyPath(kind, maskIndex);
    const FrameTime frameTime = static_cast<FrameTime>(frame);
    Vec2 localPoint;
    if (!ScenePointToLocal(*document->document, EntityId{layerId}, frameTime, {sceneX, sceneY},
                           localPoint)) {
        return;
    }
    motion::BezierPath current = CurrentBezierPath(document, layerId, propertyPath.c_str(), frameTime);
    const motion::BezierPath::Contour *contour = motion::PrimaryContour(current);
    if (contour == nullptr || index >= contour->vertices.size()) {
        return;
    }
    const Vec2 localIn = localPoint - contour->vertices[index].point;
    motion::BezierPath edited = motion::MoveInTangent(std::move(current), index, localIn, mirrorOut);
    WriteBezierPathUnlocked(document, layerId, propertyPath.c_str(), frameTime, edited);
}

void ms_command_path_edit_move_out_tangent(MSDocument *document, uint64_t layerId, MS_PATH_EDIT kind,
                                           int maskIndex, int64_t frame, size_t index, float sceneX,
                                           float sceneY, bool mirrorIn) {
    DocumentLock guard(document);
    if (document == nullptr || kind == MS_PATH_EDIT_NONE) {
        return;
    }
    const std::string propertyPath = PathEditPropertyPath(kind, maskIndex);
    const FrameTime frameTime = static_cast<FrameTime>(frame);
    Vec2 localPoint;
    if (!ScenePointToLocal(*document->document, EntityId{layerId}, frameTime, {sceneX, sceneY},
                           localPoint)) {
        return;
    }
    motion::BezierPath current = CurrentBezierPath(document, layerId, propertyPath.c_str(), frameTime);
    const motion::BezierPath::Contour *contour = motion::PrimaryContour(current);
    if (contour == nullptr || index >= contour->vertices.size()) {
        return;
    }
    const Vec2 localOut = localPoint - contour->vertices[index].point;
    motion::BezierPath edited =
        motion::MoveOutTangent(std::move(current), index, localOut, mirrorIn);
    WriteBezierPathUnlocked(document, layerId, propertyPath.c_str(), frameTime, edited);
}

void ms_command_path_edit_insert_on_segment(MSDocument *document, uint64_t layerId,
                                            MS_PATH_EDIT kind, int maskIndex, int64_t frame,
                                            size_t segmentIndex, float t) {
    DocumentLock guard(document);
    if (document == nullptr || kind == MS_PATH_EDIT_NONE) {
        return;
    }
    const std::string propertyPath = PathEditPropertyPath(kind, maskIndex);
    const FrameTime frameTime = static_cast<FrameTime>(frame);
    motion::BezierPath edited = motion::InsertVertexOnSegment(
        CurrentBezierPath(document, layerId, propertyPath.c_str(), frameTime), segmentIndex, t);
    WriteBezierPathUnlocked(document, layerId, propertyPath.c_str(), frameTime, edited);
}

void ms_command_path_edit_remove_vertex(MSDocument *document, uint64_t layerId, MS_PATH_EDIT kind,
                                        int maskIndex, int64_t frame, size_t index) {
    DocumentLock guard(document);
    if (document == nullptr || kind == MS_PATH_EDIT_NONE) {
        return;
    }
    const std::string propertyPath = PathEditPropertyPath(kind, maskIndex);
    const FrameTime frameTime = static_cast<FrameTime>(frame);
    motion::BezierPath edited = motion::RemoveVertex(
        CurrentBezierPath(document, layerId, propertyPath.c_str(), frameTime), index);
    WriteBezierPathUnlocked(document, layerId, propertyPath.c_str(), frameTime, edited);
}

void ms_command_path_edit_close(MSDocument *document, uint64_t layerId, MS_PATH_EDIT kind,
                                int maskIndex, int64_t frame) {
    DocumentLock guard(document);
    if (document == nullptr || kind == MS_PATH_EDIT_NONE) {
        return;
    }
    const std::string propertyPath = PathEditPropertyPath(kind, maskIndex);
    const FrameTime frameTime = static_cast<FrameTime>(frame);
    motion::BezierPath edited =
        motion::ClosePath(CurrentBezierPath(document, layerId, propertyPath.c_str(), frameTime));
    if (kind == MS_PATH_EDIT_SHAPE) {
        motion::Mat3 world = motion::Mat3::Identity();
        const bool hasWorld =
            LayerWorldTransform(*document->document, EntityId{layerId}, frameTime, world);
        Vec2 localCenter{};
        edited = motion::RecenterPath(std::move(edited), localCenter);
        const Animatable<Vec2> *positionProperty =
            AsVec2(FindProperty(document, layerId, "transform.position"));
        if (positionProperty != nullptr &&
            (localCenter.x != 0.0f || localCenter.y != 0.0f)) {
            const Vec2 delta = hasWorld ? world.transformVector(localCenter) : localCenter;
            const Vec2 newPosition = positionProperty->evaluate(frameTime) + delta;
            document->undoManager->beginMergeGroup();
            WriteBezierPathUnlocked(document, layerId, propertyPath.c_str(), frameTime, edited);
            WriteVec2AtPlayheadUnlocked(document, layerId, "transform.position", frameTime,
                                        newPosition);
            document->undoManager->endMergeGroup();
            return;
        }
    }
    WriteBezierPathUnlocked(document, layerId, propertyPath.c_str(), frameTime, edited);
}

void ms_command_path_edit_append_vertex(MSDocument *document, uint64_t layerId, MS_PATH_EDIT kind,
                                        int maskIndex, int64_t frame, float sceneX, float sceneY) {
    DocumentLock guard(document);
    if (document == nullptr || kind == MS_PATH_EDIT_NONE) {
        return;
    }
    const std::string propertyPath = PathEditPropertyPath(kind, maskIndex);
    const FrameTime frameTime = static_cast<FrameTime>(frame);
    Vec2 localPoint;
    if (!ScenePointToLocal(*document->document, EntityId{layerId}, frameTime, {sceneX, sceneY},
                           localPoint)) {
        return;
    }
    motion::BezierPath::Vertex vertex;
    vertex.point = localPoint;
    motion::BezierPath edited = motion::AppendVertex(
        CurrentBezierPath(document, layerId, propertyPath.c_str(), frameTime), vertex);
    WriteBezierPathUnlocked(document, layerId, propertyPath.c_str(), frameTime, edited);
}

void ms_command_path_edit_set_mirror_mode(MSDocument *document, uint64_t layerId,
                                          MS_PATH_EDIT kind, int maskIndex, int64_t frame,
                                          uint32_t vertexId, MS_VERTEX_MIRROR mode) {
    DocumentLock guard(document);
    if (document == nullptr || kind == MS_PATH_EDIT_NONE || vertexId == 0) {
        return;
    }
    const std::string propertyPath = PathEditPropertyPath(kind, maskIndex);
    const FrameTime frameTime = static_cast<FrameTime>(frame);
    motion::VectorNetwork network =
        CurrentNetwork(document, layerId, propertyPath.c_str(), frameTime);
    motion::VertexMirrorMode mirrorMode = motion::VertexMirrorMode::None;
    if (mode == MS_VERTEX_MIRROR_ANGLE) {
        mirrorMode = motion::VertexMirrorMode::Angle;
    } else if (mode == MS_VERTEX_MIRROR_ANGLE_LENGTH) {
        mirrorMode = motion::VertexMirrorMode::AngleLength;
    }
    network = motion::SetVertexMirrorMode(std::move(network), vertexId, mirrorMode);
    WriteNetworkUnlocked(document, layerId, propertyPath.c_str(), frameTime, network);
}

void ms_command_path_edit_recenter_shape(MSDocument *document, uint64_t layerId, int64_t frame) {
    DocumentLock guard(document);
    if (document == nullptr) {
        return;
    }
    RecenterShapePathUnlocked(document, layerId, static_cast<FrameTime>(frame));
}

void ms_command_network_edit_add_vertex(MSDocument *document, uint64_t layerId, MS_PATH_EDIT kind,
                                        int maskIndex, int64_t frame, float sceneX, float sceneY,
                                        uint32_t *outVertexId) {
    if (outVertexId != nullptr) {
        *outVertexId = 0;
    }
    DocumentLock guard(document);
    if (document == nullptr || kind == MS_PATH_EDIT_NONE) {
        return;
    }
    const std::string propertyPath = PathEditPropertyPath(kind, maskIndex);
    const FrameTime frameTime = static_cast<FrameTime>(frame);
    Vec2 localPoint;
    if (!ScenePointToLocal(*document->document, EntityId{layerId}, frameTime, {sceneX, sceneY},
                           localPoint)) {
        return;
    }
    uint32_t vertexId = 0;
    motion::VectorNetwork edited =
        motion::AddVertex(CurrentNetwork(document, layerId, propertyPath.c_str(), frameTime),
                          localPoint, &vertexId);
    WriteNetworkUnlocked(document, layerId, propertyPath.c_str(), frameTime, edited);
    if (outVertexId != nullptr) {
        *outVertexId = vertexId;
    }
}

void ms_command_network_edit_add_edge(MSDocument *document, uint64_t layerId, MS_PATH_EDIT kind,
                                      int maskIndex, int64_t frame, uint32_t startId, uint32_t endId,
                                      uint32_t *outEdgeId) {
    if (outEdgeId != nullptr) {
        *outEdgeId = 0;
    }
    DocumentLock guard(document);
    if (document == nullptr || kind == MS_PATH_EDIT_NONE) {
        return;
    }
    const std::string propertyPath = PathEditPropertyPath(kind, maskIndex);
    const FrameTime frameTime = static_cast<FrameTime>(frame);
    uint32_t edgeId = 0;
    motion::VectorNetwork edited =
        motion::AddEdge(CurrentNetwork(document, layerId, propertyPath.c_str(), frameTime), startId,
                        endId, &edgeId);
    if (edgeId == 0) {
        return;
    }
    WriteNetworkUnlocked(document, layerId, propertyPath.c_str(), frameTime, edited);
    if (outEdgeId != nullptr) {
        *outEdgeId = edgeId;
    }
}

void ms_command_network_edit_move_vertex(MSDocument *document, uint64_t layerId, MS_PATH_EDIT kind,
                                         int maskIndex, int64_t frame, uint32_t vertexId,
                                         float sceneX, float sceneY) {
    DocumentLock guard(document);
    if (document == nullptr || kind == MS_PATH_EDIT_NONE) {
        return;
    }
    const std::string propertyPath = PathEditPropertyPath(kind, maskIndex);
    const FrameTime frameTime = static_cast<FrameTime>(frame);
    Vec2 localPoint;
    if (!ScenePointToLocal(*document->document, EntityId{layerId}, frameTime, {sceneX, sceneY},
                           localPoint)) {
        return;
    }
    motion::VectorNetwork edited = motion::MoveVertex(
        CurrentNetwork(document, layerId, propertyPath.c_str(), frameTime), vertexId, localPoint);
    WriteNetworkUnlocked(document, layerId, propertyPath.c_str(), frameTime, edited);
}

void ms_command_network_edit_move_edge_tangent(MSDocument *document, uint64_t layerId,
                                               MS_PATH_EDIT kind, int maskIndex, int64_t frame,
                                               uint32_t edgeId, bool atStart, float sceneX,
                                               float sceneY, bool mirror) {
    DocumentLock guard(document);
    if (document == nullptr || kind == MS_PATH_EDIT_NONE) {
        return;
    }
    const std::string propertyPath = PathEditPropertyPath(kind, maskIndex);
    const FrameTime frameTime = static_cast<FrameTime>(frame);
    Vec2 localPoint;
    if (!ScenePointToLocal(*document->document, EntityId{layerId}, frameTime, {sceneX, sceneY},
                           localPoint)) {
        return;
    }
    motion::VectorNetwork network =
        CurrentNetwork(document, layerId, propertyPath.c_str(), frameTime);
    const motion::VectorNetwork::Edge *edge = motion::FindEdge(network, edgeId);
    if (edge == nullptr) {
        return;
    }
    const uint32_t endpointId = atStart ? edge->start : edge->end;
    const motion::VectorNetwork::Vertex *vertex = motion::FindVertex(network, endpointId);
    if (vertex == nullptr) {
        return;
    }
    const Vec2 tangent = localPoint - vertex->point;
    network = motion::MoveEdgeTangent(std::move(network), edgeId, atStart, tangent, mirror);
    WriteNetworkUnlocked(document, layerId, propertyPath.c_str(), frameTime, network);
}

void ms_command_network_edit_insert_on_edge(MSDocument *document, uint64_t layerId,
                                            MS_PATH_EDIT kind, int maskIndex, int64_t frame,
                                            uint32_t edgeId, float t, uint32_t *outVertexId) {
    if (outVertexId != nullptr) {
        *outVertexId = 0;
    }
    DocumentLock guard(document);
    if (document == nullptr || kind == MS_PATH_EDIT_NONE) {
        return;
    }
    const std::string propertyPath = PathEditPropertyPath(kind, maskIndex);
    const FrameTime frameTime = static_cast<FrameTime>(frame);
    uint32_t vertexId = 0;
    motion::VectorNetwork edited =
        motion::InsertVertexOnEdge(CurrentNetwork(document, layerId, propertyPath.c_str(), frameTime),
                                   edgeId, t, &vertexId);
    WriteNetworkUnlocked(document, layerId, propertyPath.c_str(), frameTime, edited);
    if (outVertexId != nullptr) {
        *outVertexId = vertexId;
    }
}

void ms_command_network_edit_remove_vertex(MSDocument *document, uint64_t layerId, MS_PATH_EDIT kind,
                                           int maskIndex, int64_t frame, uint32_t vertexId) {
    DocumentLock guard(document);
    if (document == nullptr || kind == MS_PATH_EDIT_NONE) {
        return;
    }
    const std::string propertyPath = PathEditPropertyPath(kind, maskIndex);
    const FrameTime frameTime = static_cast<FrameTime>(frame);
    motion::VectorNetwork edited = motion::RemoveVertex(
        CurrentNetwork(document, layerId, propertyPath.c_str(), frameTime), vertexId);
    WriteNetworkUnlocked(document, layerId, propertyPath.c_str(), frameTime, edited);
}

void ms_command_network_edit_recenter_shape(MSDocument *document, uint64_t layerId, int64_t frame) {
    ms_command_path_edit_recenter_shape(document, layerId, frame);
}

namespace {

Vec2 ScaleAboutPivot(Vec2 point, Vec2 pivot, float scaleX, float scaleY) {
    return Vec2{pivot.x + (point.x - pivot.x) * scaleX, pivot.y + (point.y - pivot.y) * scaleY};
}

motion::VectorNetwork ScaleNetworkAboutPivot(motion::VectorNetwork network, Vec2 pivot, float scaleX,
                                             float scaleY) {
    for (motion::VectorNetwork::Vertex &vertex : network.vertices) {
        vertex.point = ScaleAboutPivot(vertex.point, pivot, scaleX, scaleY);
    }
    for (motion::VectorNetwork::Edge &edge : network.edges) {
        edge.startTangent = Vec2{edge.startTangent.x * scaleX, edge.startTangent.y * scaleY};
        edge.endTangent = Vec2{edge.endTangent.x * scaleX, edge.endTangent.y * scaleY};
    }
    return network;
}

float ScaledExtent(float value, float scale) {
    return std::max(1.0f, std::abs(value * scale));
}

}  // namespace

bool ms_command_resize_layer_geometry(MSDocument *document, uint64_t layerId, double frameTime,
                                      float localPivotX, float localPivotY, float scaleX,
                                      float scaleY) {
    DocumentLock guard(document);
    motion::Layer *layer = FindLayer(document, layerId);
    if (document == nullptr || layer == nullptr) {
        return false;
    }
    if (std::abs(scaleX - 1.0f) < 1e-6f && std::abs(scaleY - 1.0f) < 1e-6f) {
        return true;
    }

    const FrameTime frame = static_cast<FrameTime>(frameTime);
    const Vec2 pivot{localPivotX, localPivotY};
    // Do not begin/end merge here: FreeTransform already opens a drag merge group,
    // and nesting endMergeGroup would close it mid-drag. Callers that need one
    // undo unit for a standalone call should wrap with begin/end merge group.

    if (AsVectorNetwork(FindProperty(document, layerId, "path")) != nullptr) {
        motion::VectorNetwork network = CurrentNetwork(document, layerId, "path", frame);
        if (!network.vertices.empty()) {
            WriteNetworkUnlocked(document, layerId, "path", frame,
                                 ScaleNetworkAboutPivot(std::move(network), pivot, scaleX, scaleY));
        }
    } else if (AsVec2(FindProperty(document, layerId, "size")) != nullptr &&
               AsVec2(FindProperty(document, layerId, "position")) != nullptr) {
        const Animatable<Vec2> *sizeProperty = AsVec2(FindProperty(document, layerId, "size"));
        const Animatable<Vec2> *positionProperty =
            AsVec2(FindProperty(document, layerId, "position"));
        const Vec2 size = sizeProperty->evaluate(frame);
        const Vec2 position = positionProperty->evaluate(frame);
        WriteVec2AtPlayheadUnlocked(document, layerId, "position", frame,
                                    ScaleAboutPivot(position, pivot, scaleX, scaleY));
        WriteVec2AtPlayheadUnlocked(document, layerId, "size", frame,
                                    Vec2{ScaledExtent(size.x, scaleX), ScaledExtent(size.y, scaleY)});
    }

    const int maskCount = static_cast<int>(layer->masks.size());
    for (int index = 0; index < maskCount; ++index) {
        const std::string propertyPath = PathEditPropertyPath(MS_PATH_EDIT_MASK, index);
        motion::VectorNetwork network =
            CurrentNetwork(document, layerId, propertyPath.c_str(), frame);
        if (network.vertices.empty()) {
            continue;
        }
        WriteNetworkUnlocked(document, layerId, propertyPath.c_str(), frame,
                             ScaleNetworkAboutPivot(std::move(network), pivot, scaleX, scaleY));
    }

    return true;
}
