#include "MotionStudio/undo/AddGradientStopCommand.h"

#include <algorithm>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/LayerStylePaint.h"

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
    for (size_t index = 1; index + 1 < gradient.stops.size(); ++index) {
        float previous = gradient.stops[index - 1].position.staticValue();
        float current = gradient.stops[index].position.staticValue();
        float next = gradient.stops[index + 1].position.staticValue();
        if (!(current > previous)) {
            current = std::min(previous + 0.001f, (previous + next) * 0.5f);
            gradient.stops[index].position.setStaticValue(current);
        }
        if (!(current < next)) {
            current = (previous + next) * 0.5f;
            gradient.stops[index].position.setStaticValue(current);
        }
    }
}

}  // namespace

AddGradientStopCommand::AddGradientStopCommand(EntityId layerId, int styleIndex, int insertIndex,
                                               Color color, float position)
    : layerId_(layerId)
    , styleIndex_(styleIndex)
    , insertIndex_(insertIndex)
    , color_(color)
    , position_(position) {
}

void AddGradientStopCommand::execute(Document &document) {
    GradientPaint *gradient = FindStyleGradient(document, layerId_, styleIndex_);
    if (gradient == nullptr) {
        return;
    }
    EnsureDefaultGradient(*gradient, Vec2{0, 0}, Vec2{100, 0});
    int index = insertIndex_;
    if (index < 0) {
        index = 0;
    }
    if (index > static_cast<int>(gradient->stops.size())) {
        index = static_cast<int>(gradient->stops.size());
    }
    GradientStop stop;
    stop.color.setStaticValue(color_);
    stop.position.setStaticValue(position_);
    gradient->stops.insert(gradient->stops.begin() + index, std::move(stop));
    NormalizeGradientStopEnds(*gradient);
    appliedIndex_ = index;
}

void AddGradientStopCommand::undo(Document &document) {
    if (!appliedIndex_) {
        return;
    }
    GradientPaint *gradient = FindStyleGradient(document, layerId_, styleIndex_);
    if (gradient == nullptr) {
        return;
    }
    if (*appliedIndex_ < 0 || *appliedIndex_ >= static_cast<int>(gradient->stops.size())) {
        return;
    }
    if (gradient->stops.size() <= 2u) {
        return;
    }
    gradient->stops.erase(gradient->stops.begin() + *appliedIndex_);
    NormalizeGradientStopEnds(*gradient);
}

bool AddGradientStopCommand::mergeWith(const Command &) {
    return false;
}

CommandKind AddGradientStopCommand::kind() const {
    return CommandKind::AddGradientStop;
}

std::string AddGradientStopCommand::describe() const {
    return "Add Gradient Stop";
}

}  // namespace motion
