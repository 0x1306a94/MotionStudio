#pragma once

#include <memory>
#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

class Document;

// Builds a CompositeCommand that wraps sibling layers in a new Group.
// Returns nullptr when the selection cannot be grouped. On success writes the
// new group id to outGroupId.
std::unique_ptr<Command> MakeGroupLayersCommand(
    const Document &document, EntityId compositionId,
    const std::vector<EntityId> &layerIds, EntityId &outGroupId);

// Builds a CompositeCommand that unwraps selected Group layers.
// Returns nullptr when no selected layer is a Group.
std::unique_ptr<Command> MakeUngroupLayersCommand(
    const Document &document, EntityId compositionId,
    const std::vector<EntityId> &layerIds, FrameTime time);

}  // namespace motion
