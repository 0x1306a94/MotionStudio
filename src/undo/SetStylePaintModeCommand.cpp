#include "MotionStudio/undo/SetStylePaintModeCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/LayerStylePaint.h"
#include "MotionStudio/model/ShaderUniformValues.h"

namespace motion {
namespace {

struct StylePaintState {
    StylePaintMode paintMode = StylePaintMode::Color;
    EntityId shaderId = {};
    ShaderUniformValues uniformValues;
};

bool ReadStylePaintState(LayerStyle *style, StylePaintState &out) {
    if (style == nullptr) {
        return false;
    }
    if (style->type() == LayerStyleType::Fill) {
        const auto &fill = static_cast<const FillStyle &>(*style);
        out.paintMode = fill.paintMode;
        out.shaderId = fill.shaderId;
        out.uniformValues = fill.uniformValues;
        return true;
    }
    if (style->type() == LayerStyleType::Stroke) {
        const auto &stroke = static_cast<const StrokeStyle &>(*style);
        out.paintMode = stroke.paintMode;
        out.shaderId = stroke.shaderId;
        out.uniformValues = stroke.uniformValues;
        return true;
    }
    return false;
}

bool WriteStylePaintState(LayerStyle *style, const StylePaintState &state) {
    if (style == nullptr) {
        return false;
    }
    if (style->type() == LayerStyleType::Fill) {
        auto &fill = static_cast<FillStyle &>(*style);
        fill.paintMode = state.paintMode;
        fill.shaderId = state.shaderId;
        fill.uniformValues = state.uniformValues;
        return true;
    }
    if (style->type() == LayerStyleType::Stroke) {
        auto &stroke = static_cast<StrokeStyle &>(*style);
        stroke.paintMode = state.paintMode;
        stroke.shaderId = state.shaderId;
        stroke.uniformValues = state.uniformValues;
        return true;
    }
    return false;
}

LayerStyle *FindLayerStyle(Document &document, EntityId layerId, int styleIndex) {
    Layer *layer = document.entityIndex().findLayer(layerId);
    if (layer == nullptr || styleIndex < 0 ||
        static_cast<size_t>(styleIndex) >= layer->styles.size()) {
        return nullptr;
    }
    return layer->styles[static_cast<size_t>(styleIndex)].get();
}

}  // namespace

SetStylePaintModeCommand::SetStylePaintModeCommand(EntityId layerId, int styleIndex,
                                                   StylePaintMode mode, EntityId shaderId)
    : layerId_(layerId)
    , styleIndex_(styleIndex)
    , mode_(mode)
    , shaderId_(shaderId) {
}

void SetStylePaintModeCommand::execute(Document &document) {
    LayerStyle *style = FindLayerStyle(document, layerId_, styleIndex_);
    if (style == nullptr) {
        return;
    }

    if (mode_ == StylePaintMode::Shader) {
        const ShaderDefinition *shader = FindShader(document, shaderId_);
        if (shader == nullptr) {
            return;
        }
        if (!oldPaint_) {
            StylePaintState state;
            if (!ReadStylePaintState(style, state)) {
                return;
            }
            PaintSnapshot snapshot;
            snapshot.paintMode = state.paintMode;
            snapshot.shaderId = state.shaderId;
            snapshot.uniformValues = std::move(state.uniformValues);
            oldPaint_ = std::move(snapshot);
        }
        if (style->type() == LayerStyleType::Fill) {
            BindShaderPaint(static_cast<FillStyle &>(*style), *shader);
        } else if (style->type() == LayerStyleType::Stroke) {
            BindShaderPaint(static_cast<StrokeStyle &>(*style), *shader);
        }
        return;
    }

    if (!oldPaint_) {
        StylePaintState state;
        if (!ReadStylePaintState(style, state)) {
            return;
        }
        PaintSnapshot snapshot;
        snapshot.paintMode = state.paintMode;
        snapshot.shaderId = state.shaderId;
        snapshot.uniformValues = std::move(state.uniformValues);
        oldPaint_ = std::move(snapshot);
    }
    if (style->type() == LayerStyleType::Fill) {
        ClearShaderPaint(static_cast<FillStyle &>(*style));
    } else if (style->type() == LayerStyleType::Stroke) {
        ClearShaderPaint(static_cast<StrokeStyle &>(*style));
    }
}

void SetStylePaintModeCommand::undo(Document &document) {
    if (!oldPaint_) {
        return;
    }
    LayerStyle *style = FindLayerStyle(document, layerId_, styleIndex_);
    if (style == nullptr) {
        return;
    }
    StylePaintState state;
    state.paintMode = oldPaint_->paintMode;
    state.shaderId = oldPaint_->shaderId;
    state.uniformValues = oldPaint_->uniformValues;
    WriteStylePaintState(style, state);
}

bool SetStylePaintModeCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetStylePaintMode) {
        return false;
    }
    const auto &typed = static_cast<const SetStylePaintModeCommand &>(other);
    if (typed.layerId_ != layerId_ || typed.styleIndex_ != styleIndex_) {
        return false;
    }
    mode_ = typed.mode_;
    shaderId_ = typed.shaderId_;
    return true;
}

CommandKind SetStylePaintModeCommand::kind() const {
    return CommandKind::SetStylePaintMode;
}

std::string SetStylePaintModeCommand::describe() const {
    return "Set Style Paint Mode";
}

}  // namespace motion
