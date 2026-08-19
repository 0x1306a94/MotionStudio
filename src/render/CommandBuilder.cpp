#include "MotionStudio/render/CommandBuilder.h"

#include <optional>
#include <unordered_set>
#include <utility>

#include "MotionStudio/model/TrackMatteType.h"
#include "MotionStudio/render/EvaluatedImageItem.h"
#include "MotionStudio/render/EvaluatedTextItem.h"
#include "MotionStudio/render/SelectionHandles.h"
#include "MotionStudio/render/ShapeGeometry.h"

namespace motion {

namespace {

MaskApplyMode ToMaskApplyMode(TrackMatteType type) {
    switch (type) {
        case TrackMatteType::Alpha: {
            return MaskApplyMode::AlphaMatte;
        }
        case TrackMatteType::AlphaInverted: {
            return MaskApplyMode::AlphaMatteInverted;
        }
        case TrackMatteType::Luma: {
            return MaskApplyMode::LumaMatte;
        }
        case TrackMatteType::LumaInverted: {
            return MaskApplyMode::LumaMatteInverted;
        }
        case TrackMatteType::None: {
            break;
        }
    }
    return MaskApplyMode::PathCoverage;
}

void AppendShapeItems(const std::vector<EvaluatedShapeItem> &shapeItems, BlendMode layerBlend,
                      DrawCommandList &commands) {
    BlendMode currentBlend = layerBlend;
    for (const EvaluatedShapeItem &item : shapeItems) {
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
}

void AppendImageItem(const std::optional<EvaluatedImageItem> &imageItem, DrawCommandList &commands) {
    if (!imageItem.has_value() || imageItem->absolutePath.empty()) {
        return;
    }
    DrawCommand drawImage;
    drawImage.type = DrawCommandType::DrawImage;
    drawImage.imagePath = imageItem->absolutePath;
    drawImage.imageContainerSize = imageItem->containerSize;
    drawImage.imageIntrinsicSize = imageItem->intrinsicSize;
    drawImage.imageScaleMode = imageItem->scaleMode;
    commands.push_back(std::move(drawImage));
}

void AppendTextItem(const std::optional<EvaluatedTextItem> &textItem, DrawCommandList &commands) {
    if (!textItem.has_value()) {
        return;
    }
    DrawCommand drawText;
    drawText.type = DrawCommandType::DrawText;
    drawText.textParams.text = textItem->text;
    drawText.textParams.fontSize = textItem->fontSize;
    drawText.textParams.containerSize = textItem->containerSize;
    drawText.textParams.boxTextMode = textItem->boxTextMode;
    drawText.textParams.align = textItem->align;
    drawText.textParams.fontFamily = textItem->fontFamily;
    drawText.textParams.fontStyle = textItem->fontStyle;
    drawText.textParams.styles = textItem->styles;
    if (textItem->textPath.has_value()) {
        const EvaluatedTextPath &path = *textItem->textPath;
        drawText.textParams.textPathEnabled = true;
        drawText.textParams.textPath = path.path;
        drawText.textParams.textPathReversed = path.reversed;
        drawText.textParams.textPathPerpendicular = path.perpendicular;
        drawText.textParams.textPathForceAlignment = path.forceAlignment;
        drawText.textParams.textPathFirstMargin = path.firstMargin;
        drawText.textParams.textPathLastMargin = path.lastMargin;
    }
    commands.push_back(std::move(drawText));
}

const EvaluatedLayer *FindLayer(const SceneState &state, EntityId id) {
    for (const EvaluatedLayer &layer : state.layers) {
        if (layer.id == id) {
            return &layer;
        }
    }
    return nullptr;
}

bool HasAncestor(const SceneState &state, EntityId layerId, EntityId ancestorId) {
    const EvaluatedLayer *current = FindLayer(state, layerId);
    std::unordered_set<EntityId> visiting;
    while (current != nullptr && current->parentId.isValid()) {
        if (current->parentId == ancestorId) {
            return true;
        }
        if (!visiting.insert(current->parentId).second) {
            return false;
        }
        current = FindLayer(state, current->parentId);
    }
    return false;
}

bool LayerHasDrawableContent(const EvaluatedLayer &layer) {
    return !layer.shapeItems.empty() || layer.imageItem.has_value() || layer.textItem.has_value() ||
        !layer.shapeNetwork.vertices.empty();
}

void AppendLayerAsMatte(const EvaluatedLayer &source, const Mat3 &inverseTarget, DrawCommandList &commands) {
    if (!LayerHasDrawableContent(source)) {
        return;
    }
    DrawCommand save;
    save.type = DrawCommandType::Save;
    commands.push_back(save);

    DrawCommand relative;
    relative.type = DrawCommandType::ConcatTransform;
    relative.transform = inverseTarget * source.worldTransform;
    commands.push_back(relative);

    AppendShapeItems(source.shapeItems, source.blendMode, commands);
    AppendImageItem(source.imageItem, commands);
    AppendTextItem(source.textItem, commands);

    DrawCommand restore;
    restore.type = DrawCommandType::Restore;
    commands.push_back(restore);
}

void AppendPathMasks(const std::vector<EvaluatedMask> &masks, DrawCommandList &commands) {
    DrawCommand beginMask;
    beginMask.type = DrawCommandType::BeginMask;
    beginMask.maskApplyMode = MaskApplyMode::PathCoverage;
    commands.push_back(beginMask);

    for (const EvaluatedMask &mask : masks) {
        DrawCommand drawMask;
        drawMask.type = DrawCommandType::DrawMaskPath;
        drawMask.geometry = MakePathGeometry(mask.path);
        drawMask.maskMode = mask.mode;
        drawMask.maskOpacity = mask.opacity;
        drawMask.maskInverted = mask.inverted;
        drawMask.maskFeather = mask.feather;
        drawMask.maskExpansion = mask.expansion;
        commands.push_back(std::move(drawMask));
    }

    DrawCommand endMask;
    endMask.type = DrawCommandType::EndMask;
    commands.push_back(endMask);
}

void AppendTrackMatte(const SceneState &state, const EvaluatedLayer &layer, DrawCommandList &commands) {
    DrawCommand beginMask;
    beginMask.type = DrawCommandType::BeginMask;
    beginMask.maskApplyMode = ToMaskApplyMode(layer.trackMatteType);
    commands.push_back(beginMask);

    const EvaluatedLayer *source = FindLayer(state, layer.matteSourceId);
    Mat3 inverseTarget;
    if (source != nullptr && layer.worldTransform.tryInvert(inverseTarget)) {
        AppendLayerAsMatte(*source, inverseTarget, commands);
        for (const EvaluatedLayer &candidate : state.layers) {
            if (HasAncestor(state, candidate.id, source->id)) {
                AppendLayerAsMatte(candidate, inverseTarget, commands);
            }
        }
    }

    DrawCommand endMask;
    endMask.type = DrawCommandType::EndMask;
    commands.push_back(endMask);
}

}  // namespace

DrawCommandList BuildCommands(const SceneState &state) {
    DrawCommandList commands;
    for (const EvaluatedLayer &layer : state.layers) {
        if (layer.usedAsMatteOnly || !LayerHasDrawableContent(layer)) {
            continue;
        }

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

        const bool needsIsolation = !layer.masks.empty() ||
            layer.trackMatteType != TrackMatteType::None ||
            !layer.effects.empty() ||
            !layer.layerStyles.empty();
        if (needsIsolation) {
            DrawCommand beginLayer;
            beginLayer.type = DrawCommandType::BeginLayer;
            commands.push_back(beginLayer);
        }

        AppendShapeItems(layer.shapeItems, layer.blendMode, commands);
        AppendImageItem(layer.imageItem, commands);
        AppendTextItem(layer.textItem, commands);

        if (!layer.masks.empty()) {
            AppendPathMasks(layer.masks, commands);
        }
        if (layer.trackMatteType != TrackMatteType::None) {
            AppendTrackMatte(state, layer, commands);
        }

        if (needsIsolation) {
            DrawCommand endLayer;
            endLayer.type = DrawCommandType::EndLayer;
            endLayer.effects = layer.effects;
            endLayer.layerStyles = layer.layerStyles;
            commands.push_back(std::move(endLayer));
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
                                              float handleSize,
                                              bool showAnchor,
                                              bool showScaleHandles) {
    SelectionHandles handles;
    if (!BuildSelectionHandles(state, selectedLayerIds, primaryLayerId, handles)) {
        return {};
    }
    return BuildSelectionHandleCommands(handles, strokeWidth, handleSize, showAnchor, showScaleHandles);
}

}  // namespace motion
