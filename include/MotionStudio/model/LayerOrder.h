#pragma once

#include <unordered_map>
#include <utility>
#include <vector>

#include "MotionStudio/common/EntityId.h"

namespace motion {

class Composition;

// Reorders `desired` so every layer's descendants sit immediately below it (lower index =
// drawn first = shown under the parent in the timeline). Sibling relative order and the
// relative order of subtree blocks both follow `desired`. Layers whose parentId is invalid
// or points outside `desired` are treated as roots. Ids not present in the composition are
// dropped; composition layers missing from `desired` are appended in composition order.
std::vector<EntityId> NormalizeSubtreeContiguousOrder(const std::vector<EntityId> &desired,
                                                      const Composition &composition);

// Same as above, but `parentOverrides` wins over Layer::parentId. Lets command factories
// normalize against the parent relationships they are about to establish, before those
// relationships exist in the document. Ids that appear in `parentOverrides` are kept even
// when the composition does not contain them yet, so a layer about to be added can be
// ordered alongside its future children.
std::vector<EntityId> NormalizeSubtreeContiguousOrder(
    const std::vector<EntityId> &desired, const Composition &composition,
    const std::unordered_map<EntityId, EntityId> &parentOverrides);

// True when composition.layers already satisfies the subtree-contiguous invariant: every
// layer's descendants occupy the slots immediately below it.
bool IsSubtreeContiguousOrder(const Composition &composition);

// Builds the (fromIndex, toIndex) steps that turn `from` into `to` when applied in order.
// Returns empty when the two are not permutations of each other.
std::vector<std::pair<int, int>> LayerMoveSteps(const std::vector<EntityId> &from,
                                                const std::vector<EntityId> &to);

}  // namespace motion
