#include "MotionStudio/render/CommandBuilder.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

#include "MotionStudio/render/HitTest.h"

namespace motion {

namespace {

constexpr Color kSelectionOutlineColor{0.0f, 0.47843137f, 1.0f, 1.0f};

BezierPath RectPath(Vec2 minPoint, Vec2 maxPoint) {
    BezierPath path;
    path.closed = true;
    path.vertices.push_back({{minPoint.x, minPoint.y}, {}, {}});
    path.vertices.push_back({{maxPoint.x, minPoint.y}, {}, {}});
    path.vertices.push_back({{maxPoint.x, maxPoint.y}, {}, {}});
    path.vertices.push_back({{minPoint.x, maxPoint.y}, {}, {}});
    return path;
}

}  // namespace

DrawCommandList BuildCommands(const SceneState &state) {
    DrawCommandList commands;
    for (const EvaluatedLayer &layer : state.layers) {
        DrawCommand save;
        save.type = DrawCommandType::Save;
        commands.push_back(save);

        DrawCommand opacity;
        opacity.type = DrawCommandType::SetOpacity;
        opacity.opacity = layer.opacity;
        commands.push_back(opacity);

        DrawCommand blend;
        blend.type = DrawCommandType::SetBlendMode;
        blend.blendMode = layer.blendMode;
        commands.push_back(blend);

        // Each fill carries its own blend mode; emit a SetBlendMode whenever
        // the upcoming item's blend differs from the current state.
        BlendMode currentBlend = layer.blendMode;
        for (const EvaluatedShapeItem &item : layer.shapeItems) {
            if (item.paint.blendMode != currentBlend) {
                DrawCommand itemBlend;
                itemBlend.type = DrawCommandType::SetBlendMode;
                itemBlend.blendMode = item.paint.blendMode;
                commands.push_back(itemBlend);
                currentBlend = item.paint.blendMode;
            }
            DrawCommand draw;
            draw.type = item.isStroke ? DrawCommandType::StrokePath : DrawCommandType::DrawPath;
            draw.path = item.path;
            draw.paint = item.paint;
            draw.strokeWidth = item.strokeWidth;
            draw.cap = item.cap;
            draw.join = item.join;
            commands.push_back(std::move(draw));
        }

        DrawCommand restore;
        restore.type = DrawCommandType::Restore;
        commands.push_back(restore);
    }
    return commands;
}

DrawCommandList BuildSelectionOutlineCommands(const SceneState &state,
                                              const std::vector<EntityId> &selectedLayerIds,
                                              float strokeWidth) {
    DrawCommandList commands;
    if (selectedLayerIds.empty()) {
        return commands;
    }

    std::unordered_set<EntityId> selected;
    selected.reserve(selectedLayerIds.size());
    for (EntityId id : selectedLayerIds) {
        if (id.isValid()) {
            selected.insert(id);
        }
    }
    if (selected.empty()) {
        return commands;
    }

    const float safeStrokeWidth = std::max(strokeWidth, 0.0f);
    const float padding = safeStrokeWidth * 0.5f;
    for (const EvaluatedLayer &layer : state.layers) {
        if (selected.find(layer.id) == selected.end()) {
            continue;
        }
        Vec2 minPoint;
        Vec2 maxPoint;
        if (!BoundsOfLayer(layer, minPoint, maxPoint)) {
            continue;
        }
        minPoint.x -= padding;
        minPoint.y -= padding;
        maxPoint.x += padding;
        maxPoint.y += padding;

        DrawCommand outline;
        outline.type = DrawCommandType::StrokePath;
        outline.path = RectPath(minPoint, maxPoint);
        outline.paint = Paint{kSelectionOutlineColor, FillRule::NonZero};
        outline.strokeWidth = safeStrokeWidth;
        outline.cap = LineCap::Butt;
        outline.join = LineJoin::Round;
        commands.push_back(std::move(outline));
    }
    return commands;
}

}  // namespace motion
