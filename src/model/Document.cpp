#include "MotionStudio/model/Document.h"

#include <algorithm>

#include "MotionStudio/model/ShapeContent.h"
#include "MotionStudio/model/ShapeGroup.h"

namespace motion {

namespace {

void registerShapeTree(EntityIndex& index, ShapeElement* element) {
    index.registerShape(element);
    if (auto* group = dynamic_cast<ShapeGroup*>(element)) {
        for (auto& child : group->elements) {
            registerShapeTree(index, child.get());
        }
    }
}

}  // namespace

void Document::refreshEntityIndex() {
    entityIndex_.clear();
    for (auto& composition : compositions) {
        entityIndex_.registerComposition(composition.get());
        for (auto& layer : composition->layers) {
            entityIndex_.registerLayer(layer.get());
            if (auto* shapeContent = dynamic_cast<ShapeContent*>(layer->content.get())) {
                for (auto& element : shapeContent->elements) {
                    registerShapeTree(entityIndex_, element.get());
                }
            }
        }
    }
}

Composition* Document::addComposition(std::unique_ptr<Composition> composition) {
    if (!composition) {
        return nullptr;
    }
    compositions.push_back(std::move(composition));
    refreshEntityIndex();
    return compositions.back().get();
}

std::unique_ptr<Composition> Document::takeComposition(EntityId id) {
    auto it = std::find_if(compositions.begin(), compositions.end(),
                           [&](const std::unique_ptr<Composition>& composition) {
                               return composition->id == id;
                           });
    if (it == compositions.end()) {
        return nullptr;
    }
    std::unique_ptr<Composition> taken = std::move(*it);
    compositions.erase(it);
    refreshEntityIndex();
    return taken;
}

Layer* Document::addLayer(EntityId compositionId, std::unique_ptr<Layer> layer, int index) {
    if (!layer) {
        return nullptr;
    }
    Composition* composition = entityIndex_.findComposition(compositionId);
    if (!composition) {
        return nullptr;
    }

    Layer* raw = layer.get();
    const int count = int(composition->layers.size());
    if (index < 0 || index >= count) {
        composition->layers.push_back(std::move(layer));
    } else {
        composition->layers.insert(composition->layers.begin() + index, std::move(layer));
    }
    refreshEntityIndex();
    return raw;
}

std::unique_ptr<Layer> Document::takeLayer(EntityId compositionId, EntityId layerId) {
    Composition* composition = entityIndex_.findComposition(compositionId);
    if (!composition) {
        return nullptr;
    }

    auto& layers = composition->layers;
    auto it = std::find_if(layers.begin(), layers.end(), [&](const std::unique_ptr<Layer>& entry) {
        return entry->id == layerId;
    });
    if (it == layers.end()) {
        return nullptr;
    }

    std::unique_ptr<Layer> taken = std::move(*it);
    layers.erase(it);
    refreshEntityIndex();
    return taken;
}

bool Document::moveLayer(EntityId compositionId, int fromIndex, int toIndex) {
    Composition* composition = entityIndex_.findComposition(compositionId);
    if (!composition) {
        return false;
    }

    auto& layers = composition->layers;
    const int count = int(layers.size());
    if (fromIndex < 0 || fromIndex >= count || toIndex < 0 || toIndex >= count) {
        return false;
    }
    std::unique_ptr<Layer> layer = std::move(layers[size_t(fromIndex)]);
    layers.erase(layers.begin() + fromIndex);
    layers.insert(layers.begin() + toIndex, std::move(layer));
    return true;
}

EntityIndex& Document::entityIndex() {
    return entityIndex_;
}

const EntityIndex& Document::entityIndex() const {
    return entityIndex_;
}

}  // namespace motion
