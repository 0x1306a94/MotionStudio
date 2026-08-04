#pragma once

#include <string>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/TextAlign.h"

namespace motion {

// Axis-aligned glyph bounds in text-local space after path layout.
struct TextPathBounds {
    Vec2 min{};
    Vec2 max{};
};

// Measures path-layout bounds using the same LayoutTextOnPath as drawing.
TextPathBounds MeasureTextPathBounds(const std::string &text, float fontSize, TextAlign align,
                                     const std::string &fontFamily, const std::string &fontStyle,
                                     const BezierPath &path, bool reversed, bool perpendicular,
                                     bool forceAlignment, float firstMargin, float lastMargin);

}  // namespace motion
