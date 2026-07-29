#pragma once

#include <cstdint>

#include <tgfx/core/Path.h>

#include "MotionStudio/model/FillRule.h"
#include "MotionStudio/render/ShapeGeometry.h"

namespace motion {

// Builds a tgfx path from MotionStudio geometry (relative Bezier tangents become
// absolute cubics; rect/ellipse use tgfx primitives).
tgfx::Path BuildTgfxPath(const ShapeGeometry &geometry, FillRule fillRule);

// Content hash for path-cache keys; stable for identical geometry bytes.
uint64_t HashGeometry(const ShapeGeometry &geometry, FillRule fillRule);

uint64_t MixHash(uint64_t hash, uint64_t value);
uint64_t FloatBits(float value);

}  // namespace motion
