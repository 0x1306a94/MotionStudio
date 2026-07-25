#include "MotionStudio/model/Document.h"

#include "MotionStudio/model/ShapeContent.h"

namespace motion {

void Document::refreshEntityIndex() {
    entityIndex_.clear();
    for (auto &composition : compositions) {
        entityIndex_.registerComposition(composition.get());
        for (auto &layer : composition->layers) {
            entityIndex_.registerLayer(layer.get());
            if (layer->content->type() == LayerType::Shape) {
                auto *shapeContent = static_cast<ShapeContent *>(layer->content.get());
                if (shapeContent->geometry) {
                    entityIndex_.registerShape(shapeContent->geometry.get());
                }
            }
        }
    }
}

Composition *Document::addComposition(std::unique_ptr<Composition> composition) {
    if (!composition) {
        return nullptr;
    }
    compositions.push_back(std::move(composition));
    refreshEntityIndex();
    return compositions.back().get();
}

std::unique_ptr<Composition> Document::takeComposition(EntityId id) {
    for (size_t i = 0; i < compositions.size(); ++i) {
        if (compositions[i]->id != id) {
            continue;
        }
        std::unique_ptr<Composition> taken = std::move(compositions[i]);
        compositions.erase(compositions.begin() + i);
        refreshEntityIndex();
        return taken;
    }
    return nullptr;
}

Layer *Document::addLayer(EntityId compositionId, std::unique_ptr<Layer> layer, int index) {
    if (!layer) {
        return nullptr;
    }
    Composition *composition = entityIndex_.findComposition(compositionId);
    if (!composition) {
        return nullptr;
    }

    Layer *raw = layer.get();
    const int count = static_cast<int>(composition->layers.size());
    if (index < 0 || index >= count) {
        composition->layers.push_back(std::move(layer));
    } else {
        composition->layers.insert(composition->layers.begin() + index, std::move(layer));
    }
    refreshEntityIndex();
    return raw;
}

std::unique_ptr<Layer> Document::takeLayer(EntityId compositionId, EntityId layerId) {
    Composition *composition = entityIndex_.findComposition(compositionId);
    if (!composition) {
        return nullptr;
    }

    auto &layers = composition->layers;
    for (size_t i = 0; i < layers.size(); ++i) {
        if (layers[i]->id != layerId) {
            continue;
        }
        std::unique_ptr<Layer> taken = std::move(layers[i]);
        layers.erase(layers.begin() + i);
        refreshEntityIndex();
        return taken;
    }
    return nullptr;
}

bool Document::moveLayer(EntityId compositionId, int fromIndex, int toIndex) {
    Composition *composition = entityIndex_.findComposition(compositionId);
    if (!composition) {
        return false;
    }

    auto &layers = composition->layers;
    const int count = static_cast<int>(layers.size());
    if (fromIndex < 0 || fromIndex >= count || toIndex < 0 || toIndex >= count) {
        return false;
    }
    std::unique_ptr<Layer> layer = std::move(layers[static_cast<size_t>(fromIndex)]);
    layers.erase(layers.begin() + fromIndex);
    layers.insert(layers.begin() + toIndex, std::move(layer));
    return true;
}

EntityIndex &Document::entityIndex() {
    return entityIndex_;
}

const EntityIndex &Document::entityIndex() const {
    return entityIndex_;
}

}  // namespace motion
