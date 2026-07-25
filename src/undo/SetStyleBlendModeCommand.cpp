#include "MotionStudio/undo/SetStyleBlendModeCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"

namespace motion {

namespace {

FillStyle *FindFill(Document &document, EntityId layerId, int index) {
    Layer *layer = document.entityIndex().findLayer(layerId);
    if (layer == nullptr || index < 0 ||
        static_cast<size_t>(index) >= layer->styles.size()) {
        return nullptr;
    }
    LayerStyle *style = layer->styles[static_cast<size_t>(index)].get();
    if (style->type() != LayerStyleType::Fill) {
        return nullptr;
    }
    return static_cast<FillStyle *>(style);
}

}  // namespace

SetStyleBlendModeCommand::SetStyleBlendModeCommand(EntityId layerId, int index,
                                                   BlendMode blendMode)
    : layerId_(layerId)
    , index_(index)
    , blendMode_(blendMode) {
}

void SetStyleBlendModeCommand::execute(Document &document) {
    FillStyle *fill = FindFill(document, layerId_, index_);
    if (fill == nullptr) {
        return;
    }
    if (!oldBlendMode_) {
        oldBlendMode_ = fill->blendMode;
    }
    fill->blendMode = blendMode_;
}

void SetStyleBlendModeCommand::undo(Document &document) {
    if (!oldBlendMode_) {
        return;
    }
    FillStyle *fill = FindFill(document, layerId_, index_);
    if (fill != nullptr) {
        fill->blendMode = *oldBlendMode_;
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
    return "Set Fill Blend Mode";
}

}  // namespace motion
