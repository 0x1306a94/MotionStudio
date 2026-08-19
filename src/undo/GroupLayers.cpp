#include "MotionStudio/undo/GroupLayers.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>
#include <vector>

#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/PropertyPath.h"
#include "MotionStudio/undo/AddLayerCommand.h"
#include "MotionStudio/undo/CompositeCommand.h"
#include "MotionStudio/undo/MoveLayerCommand.h"
#include "MotionStudio/undo/RemoveLayerCommand.h"
#include "MotionStudio/undo/SetParentCommand.h"
#include "MotionStudio/undo/SetStaticValueCommand.h"

namespace motion {
namespace {

bool ContainsId(const std::vector<EntityId> &ids, EntityId id) {
    for (const EntityId &candidate : ids) {
        if (candidate == id) {
            return true;
        }
    }
    return false;
}

int IndexInComposition(const Composition &composition, EntityId layerId) {
    for (int index = 0; index < static_cast<int>(composition.layers.size()); ++index) {
        if (composition.layers[static_cast<size_t>(index)]->id == layerId) {
            return index;
        }
    }
    return -1;
}

bool HasSelectedAncestor(const Document &document, EntityId layerId,
                         const std::unordered_set<EntityId> &selected) {
    const Layer *layer = document.entityIndex().findLayer(layerId);
    if (layer == nullptr) {
        return false;
    }
    EntityId cursor = layer->parentId;
    std::unordered_set<EntityId> visiting;
    while (cursor.isValid()) {
        if (selected.find(cursor) != selected.end()) {
            return true;
        }
        if (!visiting.insert(cursor).second) {
            return false;
        }
        const Layer *ancestor = document.entityIndex().findLayer(cursor);
        if (ancestor == nullptr) {
            return false;
        }
        cursor = ancestor->parentId;
    }
    return false;
}

std::vector<std::pair<int, int>> MoveSteps(const std::vector<EntityId> &from,
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

int ParentDepth(const Document &document, EntityId layerId) {
    const Layer *layer = document.entityIndex().findLayer(layerId);
    if (layer == nullptr) {
        return 0;
    }
    int depth = 0;
    EntityId cursor = layer->parentId;
    std::unordered_set<EntityId> visiting;
    while (cursor.isValid()) {
        depth += 1;
        if (!visiting.insert(cursor).second) {
            break;
        }
        const Layer *ancestor = document.entityIndex().findLayer(cursor);
        if (ancestor == nullptr) {
            break;
        }
        cursor = ancestor->parentId;
    }
    return depth;
}

void SortGroupsDeepFirst(const Document &document, std::vector<EntityId> &groups) {
    for (size_t i = 0; i < groups.size(); ++i) {
        size_t best = i;
        int bestDepth = ParentDepth(document, groups[i]);
        for (size_t j = i + 1; j < groups.size(); ++j) {
            const int depth = ParentDepth(document, groups[j]);
            if (depth > bestDepth) {
                best = j;
                bestDepth = depth;
            }
        }
        if (best != i) {
            const EntityId tmp = groups[i];
            groups[i] = groups[best];
            groups[best] = tmp;
        }
    }
}

void DecomposeLayerLocal(const Mat3 &composed, Vec2 anchor, Vec2 &position, Vec2 &scale, float &rotation) {
    const float m00 = composed.values[0];
    const float m01 = composed.values[1];
    const float m10 = composed.values[3];
    rotation = std::atan2(m10, m00) * 180.0f / 3.14159265358979323846f;
    scale.x = std::hypot(m00, m10);
    scale.y = std::hypot(m01, composed.values[4]);
    position = composed.transformPoint(anchor);
}

void CollectEffectiveChildren(const Composition &composition, EntityId groupId,
                              const std::unordered_set<EntityId> &ungrouping,
                              std::vector<EntityId> &out) {
    for (const auto &layer : composition.layers) {
        if (layer->parentId != groupId) {
            continue;
        }
        if (ungrouping.find(layer->id) != ungrouping.end()) {
            CollectEffectiveChildren(composition, layer->id, ungrouping, out);
            continue;
        }
        out.push_back(layer->id);
    }
}

Mat3 GroupChainLocal(const Document &document, EntityId fromGroupId, EntityId childId, FrameTime time) {
    const Layer *child = document.entityIndex().findLayer(childId);
    if (child == nullptr) {
        return Mat3::Identity();
    }
    std::vector<const Layer *> chain;
    EntityId cursor = child->parentId;
    std::unordered_set<EntityId> visiting;
    while (cursor.isValid()) {
        if (!visiting.insert(cursor).second) {
            break;
        }
        const Layer *layer = document.entityIndex().findLayer(cursor);
        if (layer == nullptr) {
            break;
        }
        chain.push_back(layer);
        if (cursor == fromGroupId) {
            break;
        }
        cursor = layer->parentId;
    }
    Mat3 groupLocal = Mat3::Identity();
    for (size_t index = chain.size(); index > 0; --index) {
        groupLocal = groupLocal * chain[index - 1]->localTransform(time);
    }
    return groupLocal;
}

}  // namespace

std::unique_ptr<Command> MakeGroupLayersCommand(
    const Document &document, EntityId compositionId,
    const std::vector<EntityId> &layerIds, EntityId &outGroupId) {
    outGroupId = {};
    const Composition *composition = document.entityIndex().findComposition(compositionId);
    if (composition == nullptr) {
        return nullptr;
    }

    std::vector<EntityId> candidates;
    for (const EntityId &id : layerIds) {
        if (ContainsId(candidates, id)) {
            continue;
        }
        if (IndexInComposition(*composition, id) < 0) {
            continue;
        }
        candidates.push_back(id);
    }

    std::unordered_set<EntityId> selectedSet(candidates.begin(), candidates.end());
    std::vector<EntityId> ids;
    for (const EntityId &id : candidates) {
        if (HasSelectedAncestor(document, id, selectedSet)) {
            continue;
        }
        ids.push_back(id);
    }
    if (ids.empty()) {
        return nullptr;
    }

    const Layer *first = document.entityIndex().findLayer(ids[0]);
    if (first == nullptr) {
        return nullptr;
    }
    const EntityId commonParent = first->parentId;
    for (size_t index = 1; index < ids.size(); ++index) {
        const Layer *layer = document.entityIndex().findLayer(ids[index]);
        if (layer == nullptr || layer->parentId != commonParent) {
            return nullptr;
        }
    }

    std::vector<EntityId> current;
    current.reserve(composition->layers.size());
    for (const auto &layer : composition->layers) {
        current.push_back(layer->id);
    }

    std::unordered_set<EntityId> idSet(ids.begin(), ids.end());
    int topIndex = -1;
    for (int index = 0; index < static_cast<int>(current.size()); ++index) {
        if (idSet.find(current[static_cast<size_t>(index)]) != idSet.end()) {
            topIndex = index;
        }
    }
    if (topIndex < 0) {
        return nullptr;
    }

    std::vector<EntityId> selectedOrder;
    std::vector<EntityId> remaining;
    int insertAt = 0;
    for (int index = 0; index < static_cast<int>(current.size()); ++index) {
        const EntityId id = current[static_cast<size_t>(index)];
        if (idSet.find(id) != idSet.end()) {
            selectedOrder.push_back(id);
            continue;
        }
        remaining.push_back(id);
        if (index < topIndex) {
            insertAt += 1;
        }
    }

    FrameTime inPoint = first->inPoint;
    FrameTime outPoint = first->outPoint;
    for (const EntityId &id : ids) {
        const Layer *layer = document.entityIndex().findLayer(id);
        if (layer == nullptr) {
            continue;
        }
        inPoint = std::min(inPoint, layer->inPoint);
        outPoint = std::max(outPoint, layer->outPoint);
    }

    auto group = std::make_unique<Layer>(LayerType::Group);
    group->name = "Group";
    group->parentId = commonParent;
    group->inPoint = inPoint;
    group->outPoint = outPoint;
    outGroupId = group->id;

    std::vector<EntityId> desired;
    desired.insert(desired.end(), remaining.begin(), remaining.begin() + insertAt);
    desired.insert(desired.end(), selectedOrder.begin(), selectedOrder.end());
    desired.push_back(outGroupId);
    desired.insert(desired.end(), remaining.begin() + insertAt, remaining.end());

    std::vector<EntityId> afterAdd = current;
    afterAdd.push_back(outGroupId);

    auto composite = std::make_unique<CompositeCommand>("Group");
    composite->add(std::make_unique<AddLayerCommand>(compositionId, std::move(group), -1));
    const std::vector<std::pair<int, int>> steps = MoveSteps(afterAdd, desired);
    for (const std::pair<int, int> &step : steps) {
        composite->add(std::make_unique<MoveLayerCommand>(compositionId, step.first, step.second));
    }
    for (const EntityId &id : selectedOrder) {
        composite->add(std::make_unique<SetParentCommand>(id, outGroupId));
    }
    return composite;
}

std::unique_ptr<Command> MakeUngroupLayersCommand(
    const Document &document, EntityId compositionId,
    const std::vector<EntityId> &layerIds, FrameTime time) {
    const Composition *composition = document.entityIndex().findComposition(compositionId);
    if (composition == nullptr) {
        return nullptr;
    }

    std::vector<EntityId> groups;
    for (const EntityId &id : layerIds) {
        if (ContainsId(groups, id)) {
            continue;
        }
        if (IndexInComposition(*composition, id) < 0) {
            continue;
        }
        const Layer *layer = document.entityIndex().findLayer(id);
        if (layer == nullptr || layer->type() != LayerType::Group) {
            continue;
        }
        groups.push_back(id);
    }
    if (groups.empty()) {
        return nullptr;
    }
    SortGroupsDeepFirst(document, groups);
    std::unordered_set<EntityId> ungrouping(groups.begin(), groups.end());

    auto composite = std::make_unique<CompositeCommand>("Ungroup");
    for (const EntityId &groupId : groups) {
        const Layer *group = document.entityIndex().findLayer(groupId);
        if (group == nullptr) {
            continue;
        }
        const EntityId restoredParent = group->parentId;
        std::vector<EntityId> children;
        CollectEffectiveChildren(*composition, groupId, ungrouping, children);
        for (const EntityId &childId : children) {
            Layer *child = document.entityIndex().findLayer(childId);
            if (child == nullptr) {
                continue;
            }
            const Mat3 groupLocal = GroupChainLocal(document, groupId, childId, time);
            if (groupLocal != Mat3::Identity()) {
                const Mat3 composed = groupLocal * child->localTransform(time);
                const Vec2 anchor = child->transform.anchorPoint.evaluate(time);
                Vec2 position = {};
                Vec2 scale = {};
                float rotation = 0.0f;
                DecomposeLayerLocal(composed, anchor, position, scale, rotation);
                if (!child->transform.position.isAnimated()) {
                    composite->add(std::make_unique<SetStaticValueCommand>(
                        PropertyPath{childId, "transform.position"}, PropertyValue{position}));
                }
                if (!child->transform.rotation.isAnimated()) {
                    composite->add(std::make_unique<SetStaticValueCommand>(
                        PropertyPath{childId, "transform.rotation"}, PropertyValue{rotation}));
                }
                if (!child->transform.scale.isAnimated()) {
                    composite->add(std::make_unique<SetStaticValueCommand>(
                        PropertyPath{childId, "transform.scale"}, PropertyValue{scale}));
                }
            }
            composite->add(std::make_unique<SetParentCommand>(childId, restoredParent));
        }
        composite->add(std::make_unique<RemoveLayerCommand>(compositionId, groupId));
    }
    return composite;
}

std::unique_ptr<Command> MakeRemoveLayerCommand(
    const Document &document, EntityId compositionId, EntityId layerId) {
    const Composition *composition = document.entityIndex().findComposition(compositionId);
    if (composition == nullptr) {
        return std::make_unique<RemoveLayerCommand>(compositionId, layerId);
    }

    std::unordered_set<EntityId> roots;
    roots.insert(layerId);
    std::vector<EntityId> ids;
    for (const auto &layer : composition->layers) {
        if (layer->id == layerId || HasSelectedAncestor(document, layer->id, roots)) {
            ids.push_back(layer->id);
        }
    }
    if (ids.empty()) {
        return std::make_unique<RemoveLayerCommand>(compositionId, layerId);
    }
    if (ids.size() == 1) {
        return std::make_unique<RemoveLayerCommand>(compositionId, ids[0]);
    }

    auto composite = std::make_unique<CompositeCommand>("Remove Layer");
    for (const EntityId &id : ids) {
        composite->add(std::make_unique<RemoveLayerCommand>(compositionId, id));
    }
    return composite;
}

}  // namespace motion
