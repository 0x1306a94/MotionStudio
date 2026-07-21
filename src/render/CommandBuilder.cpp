#include "MotionStudio/render/CommandBuilder.h"

#include <utility>

namespace motion {

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

        for (const EvaluatedShapeItem &item : layer.shapeItems) {
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

}  // namespace motion
