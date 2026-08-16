#include "MotionStudio/undo/SetGaussianBlurRepeatEdgeCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerEffect.h"

namespace motion {

SetGaussianBlurRepeatEdgeCommand::SetGaussianBlurRepeatEdgeCommand(EntityId layerId,
                                                                   int index,
                                                                   bool repeatEdgePixels)
    : layerId_(layerId)
    , index_(index)
    , repeatEdgePixels_(repeatEdgePixels) {
}

void SetGaussianBlurRepeatEdgeCommand::execute(Document &document) {
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || index_ < 0 ||
        static_cast<size_t>(index_) >= layer->effects.size()) {
        return;
    }
    LayerEffect *effect = layer->effects[static_cast<size_t>(index_)].get();
    if (effect->type() != LayerEffectType::GaussianBlur) {
        return;
    }
    auto *blur = static_cast<GaussianBlurEffect *>(effect);
    if (!oldRepeatEdgePixels_) {
        oldRepeatEdgePixels_ = blur->repeatEdgePixels;
    }
    blur->repeatEdgePixels = repeatEdgePixels_;
}

void SetGaussianBlurRepeatEdgeCommand::undo(Document &document) {
    if (!oldRepeatEdgePixels_) {
        return;
    }
    Layer *layer = document.entityIndex().findLayer(layerId_);
    if (layer == nullptr || index_ < 0 ||
        static_cast<size_t>(index_) >= layer->effects.size()) {
        return;
    }
    LayerEffect *effect = layer->effects[static_cast<size_t>(index_)].get();
    if (effect->type() != LayerEffectType::GaussianBlur) {
        return;
    }
    static_cast<GaussianBlurEffect *>(effect)->repeatEdgePixels = *oldRepeatEdgePixels_;
}

bool SetGaussianBlurRepeatEdgeCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetGaussianBlurRepeatEdge) {
        return false;
    }
    const auto &typed = static_cast<const SetGaussianBlurRepeatEdgeCommand &>(other);
    if (typed.layerId_ != layerId_ || typed.index_ != index_) {
        return false;
    }
    repeatEdgePixels_ = typed.repeatEdgePixels_;
    return true;
}

CommandKind SetGaussianBlurRepeatEdgeCommand::kind() const {
    return CommandKind::SetGaussianBlurRepeatEdge;
}

std::string SetGaussianBlurRepeatEdgeCommand::describe() const {
    return "Set Blur Repeat Edge";
}

}  // namespace motion
