#pragma once

#include <vector>

#include "MotionStudio/model/StrokeMode.h"

namespace motion {

// Clamps negatives to 0, duplicates odd-length arrays, and returns empty when
// the pattern cannot produce dashes (fewer than two intervals or zero sum).
// dashes: raw on/off lengths in pixels along path arc length.
std::vector<float> NormalizeDashArray(std::vector<float> dashes);

// True when strokeMode is Dashed and NormalizeDashArray(dashes) is non-empty.
// strokeMode: Solid skips dashing even if dashes is populated.
// dashes: stored pattern; not mutated.
bool NeedsDash(StrokeMode strokeMode, const std::vector<float> &dashes);

// Default on/off pair used when switching Solid → Dashed with an empty pattern.
std::vector<float> DefaultDashPattern();

}  // namespace motion
