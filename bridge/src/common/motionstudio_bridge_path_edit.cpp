#include "motionstudio_bridge.h"

#include <memory>
#include <string>
#include <utility>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/PathGeometryEdit.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/Document.h"
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
    if (property == nullptr || property->valueType() != AnimatableType::BezierPath) {
        return;
    }
    if (static_cast<Animatable<motion::BezierPath> *>(property)->isAnimated()) {
        Execute(document,
                std::make_unique<motion::AddKeyframeCommand>(
                    MakePath(entityId, path),
                    motion::KeyframeData(MakeKeyframe(frame, value))));
        return;
    }
    Execute(document, std::make_unique<motion::SetStaticValueCommand>(MakePath(entityId, path),
                                                                      motion::PropertyValue(value)));
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
    Execute(document, std::make_unique<motion::SetStaticValueCommand>(MakePath(entityId, path),
                                                                      motion::PropertyValue(value)));
}

motion::BezierPath CurrentBezierPath(MSDocument *document, uint64_t entityId, const char *path,
                                     FrameTime frame) {
    const Animatable<motion::BezierPath> *property =
        AsBezierPath(FindProperty(document, entityId, path));
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
    motion::BezierPath path = CurrentBezierPath(document, layerId, "path", frame);
    if (path.vertices.empty()) {
        return;
    }
    motion::Mat3 world = motion::Mat3::Identity();
    const bool hasWorld = LayerWorldTransform(*document->document, EntityId{layerId}, frame, world);
    Vec2 localCenter{};
    path = motion::RecenterPath(std::move(path), localCenter);
    if (localCenter.x == 0.0f && localCenter.y == 0.0f) {
        return;
    }
    const Animatable<Vec2> *positionProperty =
        AsVec2(FindProperty(document, layerId, "transform.position"));
    if (positionProperty == nullptr) {
        WriteBezierPathUnlocked(document, layerId, "path", frame, path);
        return;
    }
    const Vec2 delta = hasWorld ? world.transformVector(localCenter) : localCenter;
    const Vec2 newPosition = positionProperty->evaluate(frame) + delta;
    document->undoManager->beginMergeGroup();
    WriteBezierPathUnlocked(document, layerId, "path", frame, path);
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
    if (index >= current.vertices.size()) {
        return;
    }
    const Vec2 localIn = localPoint - current.vertices[index].point;
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
    if (index >= current.vertices.size()) {
        return;
    }
    const Vec2 localOut = localPoint - current.vertices[index].point;
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

void ms_command_path_edit_toggle_smooth(MSDocument *document, uint64_t layerId, MS_PATH_EDIT kind,
                                        int maskIndex, int64_t frame, size_t index) {
    DocumentLock guard(document);
    if (document == nullptr || kind == MS_PATH_EDIT_NONE) {
        return;
    }
    const std::string propertyPath = PathEditPropertyPath(kind, maskIndex);
    const FrameTime frameTime = static_cast<FrameTime>(frame);
    motion::BezierPath edited = motion::ToggleVertexSmooth(
        CurrentBezierPath(document, layerId, propertyPath.c_str(), frameTime), index);
    WriteBezierPathUnlocked(document, layerId, propertyPath.c_str(), frameTime, edited);
}

void ms_command_path_edit_recenter_shape(MSDocument *document, uint64_t layerId, int64_t frame) {
    DocumentLock guard(document);
    if (document == nullptr) {
        return;
    }
    RecenterShapePathUnlocked(document, layerId, static_cast<FrameTime>(frame));
}
