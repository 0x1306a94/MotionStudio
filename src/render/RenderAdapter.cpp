#include "MotionStudio/render/RenderAdapter.h"

namespace motion {

void PlayCommands(const DrawCommandList &commands, RenderAdapter &adapter) {
    for (const DrawCommand &command : commands) {
        switch (command.type) {
            case DrawCommandType::Save: {
                adapter.save();
                break;
            }
            case DrawCommandType::Restore: {
                adapter.restore();
                break;
            }
            case DrawCommandType::ConcatTransform: {
                adapter.concatTransform(command.transform);
                break;
            }
            case DrawCommandType::SetOpacity: {
                adapter.setOpacity(command.opacity);
                break;
            }
            case DrawCommandType::SetBlendMode: {
                adapter.setBlendMode(command.blendMode);
                break;
            }
            case DrawCommandType::DrawPath: {
                adapter.drawPath(command.geometry, command.paint);
                break;
            }
            case DrawCommandType::StrokePath: {
                adapter.strokePath(command.geometry, command.paint, command.stroke);
                break;
            }
            case DrawCommandType::ClipPath: {
                adapter.clipPath(command.geometry, command.fillRule);
                break;
            }
        }
    }
}

}  // namespace motion
