#include "MotionStudio/undo/SetLayerFxBlendModeCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerFx.h"

namespace motion {

namespace {

BlendMode *FindLayerFxBlendMode(Document &document, EntityId layerId, int index) {
    Layer *layer = document.entityIndex().findLayer(layerId);
    if (layer == nullptr || index < 0 ||
        static_cast<size_t>(index) >= layer->layerStyles.size()) {
        return nullptr;
    }
    LayerFx *style = layer->layerStyles[static_cast<size_t>(index)].get();
    switch (style->type()) {
        case LayerFxType::DropShadow: {
            return &static_cast<DropShadowStyle *>(style)->blendMode;
        }
        case LayerFxType::OuterGlow: {
            return &static_cast<OuterGlowStyle *>(style)->blendMode;
        }
        case LayerFxType::Stroke: {
            return &static_cast<LayerStrokeStyle *>(style)->blendMode;
        }
    }
    return nullptr;
}

}  // namespace

SetLayerFxBlendModeCommand::SetLayerFxBlendModeCommand(EntityId layerId, int index,
                                                       BlendMode blendMode)
    : layerId_(layerId)
    , index_(index)
    , blendMode_(blendMode) {
}

void SetLayerFxBlendModeCommand::execute(Document &document) {
    BlendMode *target = FindLayerFxBlendMode(document, layerId_, index_);
    if (target == nullptr) {
        return;
    }
    if (!oldBlendMode_) {
        oldBlendMode_ = *target;
    }
    *target = blendMode_;
}

void SetLayerFxBlendModeCommand::undo(Document &document) {
    if (!oldBlendMode_) {
        return;
    }
    BlendMode *target = FindLayerFxBlendMode(document, layerId_, index_);
    if (target != nullptr) {
        *target = *oldBlendMode_;
    }
}

bool SetLayerFxBlendModeCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetLayerFxBlendMode) {
        return false;
    }
    const auto &typed = static_cast<const SetLayerFxBlendModeCommand &>(other);
    if (typed.layerId_ != layerId_ || typed.index_ != index_) {
        return false;
    }
    blendMode_ = typed.blendMode_;
    return true;
}

CommandKind SetLayerFxBlendModeCommand::kind() const {
    return CommandKind::SetLayerFxBlendMode;
}

std::string SetLayerFxBlendModeCommand::describe() const {
    return "Set Layer Style Blend Mode";
}

}  // namespace motion
