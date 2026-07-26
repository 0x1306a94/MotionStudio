#include "MotionStudio/render/CommandBuilder.h"

#include <utility>

#include "MotionStudio/render/SelectionHandles.h"

namespace motion {

DrawCommandList BuildCommands(const SceneState &state) {
    DrawCommandList commands;
    for (const EvaluatedLayer &layer : state.layers) {
        DrawCommand save;
        save.type = DrawCommandType::Save;
        commands.push_back(save);

        DrawCommand transform;
        transform.type = DrawCommandType::ConcatTransform;
        transform.transform = layer.worldTransform;
        commands.push_back(transform);

        DrawCommand opacity;
        opacity.type = DrawCommandType::SetOpacity;
        opacity.opacity = layer.opacity;
        commands.push_back(opacity);

        DrawCommand blend;
        blend.type = DrawCommandType::SetBlendMode;
        blend.blendMode = layer.blendMode;
        commands.push_back(blend);

        // Each style carries its own blend mode; emit a SetBlendMode whenever
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
            draw.geometry = item.geometry;
            draw.paint = item.paint;
            draw.stroke = item.stroke;
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
                                              EntityId primaryLayerId,
                                              float strokeWidth,
                                              float handleSize) {
    SelectionHandles handles;
    if (!BuildSelectionHandles(state, selectedLayerIds, primaryLayerId, handles)) {
        return {};
    }
    return BuildSelectionHandleCommands(handles, strokeWidth, handleSize);
}

}  // namespace motion
