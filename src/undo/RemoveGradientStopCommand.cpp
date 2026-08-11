#include "MotionStudio/undo/RemoveGradientStopCommand.h"

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

void NormalizeGradientStopEnds(GradientPaint &gradient) {
    if (gradient.stops.empty()) {
        return;
    }
    gradient.stops.front().position.setStaticValue(0.f);
    gradient.stops.back().position.setStaticValue(1.f);
}

}  // namespace

RemoveGradientStopCommand::RemoveGradientStopCommand(EntityId layerId, int styleIndex,
                                                     int stopIndex)
    : layerId_(layerId)
    , styleIndex_(styleIndex)
    , stopIndex_(stopIndex) {
}

void RemoveGradientStopCommand::execute(Document &document) {
    GradientPaint *gradient = FindStyleGradient(document, layerId_, styleIndex_);
    if (gradient == nullptr) {
        return;
    }
    if (gradient->stops.size() <= 2u) {
        return;
    }
    if (stopIndex_ < 0 || stopIndex_ >= static_cast<int>(gradient->stops.size())) {
        return;
    }
    if (!removedStop_) {
        removedStop_ = gradient->stops[static_cast<size_t>(stopIndex_)];
    }
    gradient->stops.erase(gradient->stops.begin() + stopIndex_);
    NormalizeGradientStopEnds(*gradient);
}

void RemoveGradientStopCommand::undo(Document &document) {
    if (!removedStop_) {
        return;
    }
    GradientPaint *gradient = FindStyleGradient(document, layerId_, styleIndex_);
    if (gradient == nullptr) {
        return;
    }
    int index = stopIndex_;
    if (index < 0) {
        index = 0;
    }
    if (index > static_cast<int>(gradient->stops.size())) {
        index = static_cast<int>(gradient->stops.size());
    }
    gradient->stops.insert(gradient->stops.begin() + index, *removedStop_);
    NormalizeGradientStopEnds(*gradient);
}

bool RemoveGradientStopCommand::mergeWith(const Command &) {
    return false;
}

CommandKind RemoveGradientStopCommand::kind() const {
    return CommandKind::RemoveGradientStop;
}

std::string RemoveGradientStopCommand::describe() const {
    return "Remove Gradient Stop";
}

}  // namespace motion
