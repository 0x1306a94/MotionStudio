#pragma once

#include "MotionStudio/common/Vec2.h"

namespace motion {

// Scales an anchor point with a container size change so the relative
// position inside the box is preserved (position stays unchanged).
// If an old size axis is 0, that axis of the anchor is left unchanged.
inline Vec2 ScaleAnchorForSizeChange(Vec2 oldSize, Vec2 newSize, Vec2 oldAnchor) {
    const float x = oldSize.x != 0.0f ? oldAnchor.x * (newSize.x / oldSize.x) : oldAnchor.x;
    const float y = oldSize.y != 0.0f ? oldAnchor.y * (newSize.y / oldSize.y) : oldAnchor.y;
    return Vec2{x, y};
}

}  // namespace motion
