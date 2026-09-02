#include "PreviewTransformApply.h"

namespace bridge {

void ApplyPreviewTransformsToScene(motion::SceneState &state, const std::unordered_map<motion::EntityId, motion::Mat3> &preview) {
    if (preview.empty()) {
        return;
    }
    for (motion::EvaluatedLayer &layer : state.layers) {
        const auto found = preview.find(layer.id);
        if (found == preview.end()) {
            continue;
        }
        layer.worldTransform = layer.worldTransform * found->second;
    }
}

}  // namespace bridge
