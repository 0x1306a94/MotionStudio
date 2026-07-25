#include "MotionStudio/undo/SetStyleBlendModeCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"

namespace motion {

namespace {

BlendMode *FindStyleBlendMode(Document &document, EntityId layerId, int index) {
    Layer *layer = document.entityIndex().findLayer(layerId);
    if (layer == nullptr || index < 0 ||
        static_cast<size_t>(index) >= layer->styles.size()) {
        return nullptr;
    }
    LayerStyle *style = layer->styles[static_cast<size_t>(index)].get();
    switch (style->type()) {
        case LayerStyleType::Fill: {
            return &static_cast<FillStyle *>(style)->blendMode;
        }
        case LayerStyleType::Stroke: {
            return &static_cast<StrokeStyle *>(style)->blendMode;
        }
    }
    return nullptr;
}

}  // namespace

SetStyleBlendModeCommand::SetStyleBlendModeCommand(EntityId layerId, int index,
                                                   BlendMode blendMode)
    : layerId_(layerId)
    , index_(index)
    , blendMode_(blendMode) {
}

void SetStyleBlendModeCommand::execute(Document &document) {
    BlendMode *target = FindStyleBlendMode(document, layerId_, index_);
    if (target == nullptr) {
        return;
    }
    if (!oldBlendMode_) {
        oldBlendMode_ = *target;
    }
    *target = blendMode_;
}

void SetStyleBlendModeCommand::undo(Document &document) {
    if (!oldBlendMode_) {
        return;
    }
    BlendMode *target = FindStyleBlendMode(document, layerId_, index_);
    if (target != nullptr) {
        *target = *oldBlendMode_;
    }
}

bool SetStyleBlendModeCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetStyleBlendMode) {
        return false;
    }
    const auto &typed = static_cast<const SetStyleBlendModeCommand &>(other);
    if (typed.layerId_ != layerId_ || typed.index_ != index_) {
        return false;
    }
    blendMode_ = typed.blendMode_;  // Preserve oldBlendMode_, absorb final value.
    return true;
}

CommandKind SetStyleBlendModeCommand::kind() const {
    return CommandKind::SetStyleBlendMode;
}

std::string SetStyleBlendModeCommand::describe() const {
    return "Set Style Blend Mode";
}

}  // namespace motion
