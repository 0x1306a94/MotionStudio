#pragma once

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"

namespace motion {
namespace {

StrokeStyle *FindStroke(Document &document, EntityId layerId, int index) {
    Layer *layer = document.entityIndex().findLayer(layerId);
    if (layer == nullptr || index < 0 ||
        static_cast<size_t>(index) >= layer->styles.size()) {
        return nullptr;
    }
    LayerStyle *style = layer->styles[static_cast<size_t>(index)].get();
    if (style->type() != LayerStyleType::Stroke) {
        return nullptr;
    }
    return static_cast<StrokeStyle *>(style);
}

}  // namespace
}  // namespace motion
