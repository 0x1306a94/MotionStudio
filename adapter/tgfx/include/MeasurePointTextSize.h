#pragma once

#include <string>

#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/TextAlign.h"

namespace motion {

// Point-text layout measure (softWrap off): content width/height from glyph metrics.
// Returns at least Vec2{1, 1}. Falls back to {1,1} if the typeface cannot be resolved.
Vec2 MeasurePointTextSize(const std::string &text, float fontSize, TextAlign align,
                          const std::string &fontFamily, const std::string &fontStyle);

}  // namespace motion
