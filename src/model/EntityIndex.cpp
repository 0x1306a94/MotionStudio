#include "MotionStudio/model/EntityIndex.h"

#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/ShapeElement.h"

namespace motion {

Layer* EntityIndex::findLayer(EntityId id) const {
    auto it = layers_.find(id);
    return it == layers_.end() ? nullptr : it->second;
}

Composition* EntityIndex::findComposition(EntityId id) const {
    auto it = compositions_.find(id);
    return it == compositions_.end() ? nullptr : it->second;
}

ShapeElement* EntityIndex::findShape(EntityId id) const {
    auto it = shapes_.find(id);
    return it == shapes_.end() ? nullptr : it->second;
}

void EntityIndex::registerLayer(Layer* layer) {
    layers_[layer->id] = layer;
}

void EntityIndex::registerComposition(Composition* composition) {
    compositions_[composition->id] = composition;
}

void EntityIndex::registerShape(ShapeElement* shape) {
    shapes_[shape->id] = shape;
}

void EntityIndex::clear() {
    layers_.clear();
    compositions_.clear();
    shapes_.clear();
}

}  // namespace motion
