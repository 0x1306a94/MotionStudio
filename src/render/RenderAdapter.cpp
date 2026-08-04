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
            case DrawCommandType::BeginLayer: {
                adapter.beginLayer();
                break;
            }
            case DrawCommandType::EndLayer: {
                adapter.endLayer();
                break;
            }
            case DrawCommandType::BeginMask: {
                adapter.beginMask(command.maskApplyMode);
                break;
            }
            case DrawCommandType::EndMask: {
                adapter.endMask();
                break;
            }
            case DrawCommandType::DrawMaskPath: {
                adapter.drawMaskPath(command.geometry, command.maskMode, command.maskOpacity,
                                     command.maskInverted, command.maskFeather,
                                     command.maskExpansion);
                break;
            }
            case DrawCommandType::DrawImage: {
                adapter.drawImage(command.imagePath, command.imageContainerSize,
                                  command.imageIntrinsicSize, command.imageScaleMode);
                break;
            }
            case DrawCommandType::DrawText: {
                adapter.drawText(command.textParams);
                break;
            }
        }
    }
}

}  // namespace motion
