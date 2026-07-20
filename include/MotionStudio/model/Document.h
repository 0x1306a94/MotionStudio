#pragma once

#include <memory>
#include <string>
#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/Asset.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/EntityIndex.h"

namespace motion {

// Top-level document containing compositions, assets, and the entity index.
// Structural mutations automatically keep the entity index up to date.
class Document {
public:
    // ---- Structural mutations (entity index refreshed automatically) ----

    // Adds a composition. Returns the inserted raw pointer (ownership stays in
    // Document) or nullptr if composition is null.
    // composition: the composition to add.
    Composition* addComposition(std::unique_ptr<Composition> composition);
    // Removes and returns a composition (including all its layers).
    // Returns nullptr if not found.
    // id: id of the composition to remove.
    std::unique_ptr<Composition> takeComposition(EntityId id);

    // Inserts a layer into a composition. Appends to the end when index < 0 or
    // out of range. Returns the inserted raw pointer.
    // compositionId: host composition of the layer.
    // layer: the layer to add.
    // index: insertion position (-1 to append).
    Layer* addLayer(EntityId compositionId, std::unique_ptr<Layer> layer, int index = -1);
    // Removes and returns a layer from a composition.
    // Returns nullptr if not found.
    // compositionId: host composition of the layer.
    // layerId: id of the layer to remove.
    std::unique_ptr<Layer> takeLayer(EntityId compositionId, EntityId layerId);
    // Reorders a layer within a composition. Returns false on out-of-range index.
    // compositionId: host composition of the layer.
    // fromIndex: current position of the layer.
    // toIndex: desired position after the move.
    bool moveLayer(EntityId compositionId, int fromIndex, int toIndex);

    // ---- Queries ----

    EntityIndex& entityIndex();
    const EntityIndex& entityIndex() const;

    // Walks the entire entity tree to rebuild the index. Call once after batch
    // construction (e.g. deserialization).
    void refreshEntityIndex();

    EntityId id = EntityId::Generate();
    std::string name;
    std::vector<std::unique_ptr<Composition>> compositions;
    std::vector<Asset> assets;  // document-level resources (images, fonts)

private:
    EntityIndex entityIndex_;
};

}  // namespace motion
