#pragma once

#include <unordered_map>

#include "MotionStudio/common/EntityId.h"

namespace motion {

class Composition;
class Layer;
class ShapeElement;

// Global flat index from EntityId to entity pointer for O(1) lookup.
// Used by undo commands and bridge layers to resolve targets: commands hold
// only EntityId, so a deleted entity resolves to nullptr.
class EntityIndex {
public:
    // id: entity id to look up. Returns nullptr if not registered.
    Layer* findLayer(EntityId id) const;
    Composition* findComposition(EntityId id) const;
    ShapeElement* findShape(EntityId id) const;

    // layer: non-owning pointer to register.
    void registerLayer(Layer* layer);
    void registerComposition(Composition* composition);
    void registerShape(ShapeElement* shape);
    void clear();

private:
    std::unordered_map<EntityId, Layer*> layers_;
    std::unordered_map<EntityId, Composition*> compositions_;
    std::unordered_map<EntityId, ShapeElement*> shapes_;
};

}  // namespace motion
