#include "MotionStudio/render/PathOverlay.h"

#include <unordered_set>
#include <utility>

#include "MotionStudio/render/ShapeGeometry.h"

namespace motion {

namespace {

void AppendOverlayStroke(DrawCommandList &commands, const PathOverlayItem &item, float strokeWidth) {
    if (item.path.vertices.empty()) {
        return;
    }

    DrawCommand save;
    save.type = DrawCommandType::Save;
    commands.push_back(save);

    DrawCommand transform;
    transform.type = DrawCommandType::ConcatTransform;
    transform.transform = item.worldTransform;
    commands.push_back(transform);

    DrawCommand stroke;
    stroke.type = DrawCommandType::StrokePath;
    stroke.geometry = MakePathGeometry(item.path);
    stroke.paint = Paint{item.color, FillRule::NonZero};
    stroke.stroke.width = strokeWidth;
    stroke.stroke.join = LineJoin::Miter;
    commands.push_back(std::move(stroke));

    DrawCommand restore;
    restore.type = DrawCommandType::Restore;
    commands.push_back(restore);
}

const EvaluatedLayer *FindEvaluatedLayer(const SceneState &state, EntityId id) {
    for (const EvaluatedLayer &layer : state.layers) {
        if (layer.id == id) {
            return &layer;
        }
    }
    return nullptr;
}

}  // namespace

DrawCommandList BuildPathOverlayCommands(const std::vector<PathOverlayItem> &items,
                                         float strokeWidth) {
    DrawCommandList commands;
    const float safeStroke = strokeWidth > 0.0f ? strokeWidth : 0.0f;
    for (const PathOverlayItem &item : items) {
        AppendOverlayStroke(commands, item, safeStroke);
    }
    return commands;
}

std::vector<PathOverlayItem> CollectMaskPathOverlays(const SceneState &state,
                                                     const std::vector<EntityId> &selectedLayerIds,
                                                     Color color) {
    std::vector<PathOverlayItem> items;
    std::unordered_set<EntityId> seen;
    seen.reserve(selectedLayerIds.size());
    for (EntityId id : selectedLayerIds) {
        if (!id.isValid() || seen.find(id) != seen.end()) {
            continue;
        }
        seen.insert(id);
        const EvaluatedLayer *layer = FindEvaluatedLayer(state, id);
        if (layer == nullptr) {
            continue;
        }
        for (const EvaluatedMask &mask : layer->masks) {
            if (mask.path.vertices.empty()) {
                continue;
            }
            PathOverlayItem item;
            item.worldTransform = layer->worldTransform;
            item.path = mask.path;
            item.color = color;
            items.push_back(std::move(item));
        }
    }
    return items;
}

}  // namespace motion
