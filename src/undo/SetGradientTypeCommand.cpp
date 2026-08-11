#include "MotionStudio/undo/SetGradientTypeCommand.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"

namespace motion {
namespace {

GradientPaint *FindStyleGradient(Document &document, EntityId layerId, int styleIndex) {
    Layer *layer = document.entityIndex().findLayer(layerId);
    if (layer == nullptr || styleIndex < 0 ||
        static_cast<size_t>(styleIndex) >= layer->styles.size()) {
        return nullptr;
    }
    LayerStyle *style = layer->styles[static_cast<size_t>(styleIndex)].get();
    if (style->type() == LayerStyleType::Fill) {
        return &static_cast<FillStyle *>(style)->gradient;
    }
    if (style->type() == LayerStyleType::Stroke) {
        return &static_cast<StrokeStyle *>(style)->gradient;
    }
    return nullptr;
}

}  // namespace

SetGradientTypeCommand::SetGradientTypeCommand(EntityId layerId, int styleIndex, GradientType type)
    : layerId_(layerId)
    , styleIndex_(styleIndex)
    , type_(type) {
}

void SetGradientTypeCommand::execute(Document &document) {
    GradientPaint *gradient = FindStyleGradient(document, layerId_, styleIndex_);
    if (gradient == nullptr) {
        return;
    }
    if (!oldType_) {
        oldType_ = gradient->type;
    }
    gradient->type = type_;
}

void SetGradientTypeCommand::undo(Document &document) {
    if (!oldType_) {
        return;
    }
    GradientPaint *gradient = FindStyleGradient(document, layerId_, styleIndex_);
    if (gradient != nullptr) {
        gradient->type = *oldType_;
    }
}

bool SetGradientTypeCommand::mergeWith(const Command &other) {
    if (other.kind() != CommandKind::SetGradientType) {
        return false;
    }
    const auto &typed = static_cast<const SetGradientTypeCommand &>(other);
    if (typed.layerId_ != layerId_ || typed.styleIndex_ != styleIndex_) {
        return false;
    }
    type_ = typed.type_;
    return true;
}

CommandKind SetGradientTypeCommand::kind() const {
    return CommandKind::SetGradientType;
}

std::string SetGradientTypeCommand::describe() const {
    return "Set Gradient Type";
}

}  // namespace motion
