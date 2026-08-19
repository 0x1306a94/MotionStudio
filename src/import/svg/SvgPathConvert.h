#pragma once

#include "MotionStudio/common/VectorNetwork.h"
#include "tgfx/core/Path.h"

namespace motion {
namespace svg {

// Converts a tgfx path into an editable VectorNetwork. usedConic is set when any
// conic segment is approximated as a cubic.
VectorNetwork PathToVectorNetwork(const tgfx::Path &path, bool *usedConic);

}  // namespace svg
}  // namespace motion
