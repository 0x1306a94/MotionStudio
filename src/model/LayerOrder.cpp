#include "MotionStudio/model/LayerOrder.h"

#include <unordered_set>

#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Layer.h"

namespace motion {
namespace {

struct SubtreeWalk {
    std::unordered_map<EntityId, std::vector<EntityId>> childrenOf;
    std::unordered_set<EntityId> visited;
    std::vector<EntityId> result;
};

EntityId EffectiveParent(EntityId layerId, const Layer &layer,
                         const std::unordered_map<EntityId, EntityId> &parentOverrides) {
    const auto found = parentOverrides.find(layerId);
    if (found != parentOverrides.end()) {
        return found->second;
    }
    return layer.parentId;
}

// Descendants are emitted before their parent so they land on lower indices, which the
// timeline renders as rows nested under the parent.
void EmitSubtree(SubtreeWalk &walk, EntityId layerId) {
    if (!walk.visited.insert(layerId).second) {
        return;
    }
    const auto children = walk.childrenOf.find(layerId);
    if (children != walk.childrenOf.end()) {
        for (const EntityId &childId : children->second) {
            EmitSubtree(walk, childId);
        }
    }
    walk.result.push_back(layerId);
}

const Layer *FindLayer(const Composition &composition, EntityId layerId) {
    for (const auto &layer : composition.layers) {
        if (layer->id == layerId) {
            return layer.get();
        }
    }
    return nullptr;
}

}  // namespace

std::vector<EntityId> NormalizeSubtreeContiguousOrder(const std::vector<EntityId> &desired,
                                                      const Composition &composition) {
    return NormalizeSubtreeContiguousOrder(desired, composition, {});
}

std::vector<EntityId> NormalizeSubtreeContiguousOrder(
    const std::vector<EntityId> &desired, const Composition &composition,
    const std::unordered_map<EntityId, EntityId> &parentOverrides) {
    std::vector<EntityId> ordered;
    std::unordered_set<EntityId> present;
    for (const EntityId &id : desired) {
        if (FindLayer(composition, id) == nullptr) {
            continue;
        }
        if (!present.insert(id).second) {
            continue;
        }
        ordered.push_back(id);
    }
    for (const auto &layer : composition.layers) {
        if (present.insert(layer->id).second) {
            ordered.push_back(layer->id);
        }
    }

    SubtreeWalk walk;
    std::vector<EntityId> roots;
    for (const EntityId &id : ordered) {
        const Layer *layer = FindLayer(composition, id);
        if (layer == nullptr) {
            continue;
        }
        const EntityId parentId = EffectiveParent(id, *layer, parentOverrides);
        if (parentId.isValid() && present.find(parentId) != present.end()) {
            walk.childrenOf[parentId].push_back(id);
            continue;
        }
        roots.push_back(id);
    }

    walk.result.reserve(ordered.size());
    for (const EntityId &rootId : roots) {
        EmitSubtree(walk, rootId);
    }
    // A parentId cycle keeps its members out of `roots`, so emit whatever the walk skipped.
    for (const EntityId &id : ordered) {
        EmitSubtree(walk, id);
    }
    return walk.result;
}

bool IsSubtreeContiguousOrder(const Composition &composition) {
    std::vector<EntityId> current;
    current.reserve(composition.layers.size());
    for (const auto &layer : composition.layers) {
        current.push_back(layer->id);
    }
    return NormalizeSubtreeContiguousOrder(current, composition) == current;
}

std::vector<std::pair<int, int>> LayerMoveSteps(const std::vector<EntityId> &from,
                                                const std::vector<EntityId> &to) {
    if (from.size() != to.size()) {
        return {};
    }
    std::vector<EntityId> order = from;
    std::vector<std::pair<int, int>> steps;
    for (size_t targetIndex = 0; targetIndex < to.size(); ++targetIndex) {
        const EntityId wanted = to[targetIndex];
        int sourceIndex = -1;
        for (size_t index = 0; index < order.size(); ++index) {
            if (order[index] == wanted) {
                sourceIndex = static_cast<int>(index);
                break;
            }
        }
        if (sourceIndex < 0) {
            return {};
        }
        if (sourceIndex == static_cast<int>(targetIndex)) {
            continue;
        }
        const EntityId layerId = order[static_cast<size_t>(sourceIndex)];
        order.erase(order.begin() + sourceIndex);
        order.insert(order.begin() + static_cast<std::ptrdiff_t>(targetIndex), layerId);
        steps.push_back({sourceIndex, static_cast<int>(targetIndex)});
    }
    return steps;
}

}  // namespace motion
