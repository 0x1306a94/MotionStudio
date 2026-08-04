#pragma once

#include <string>
#include <vector>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/TextAlign.h"

namespace motion {

// Input for laying out point text along a path (text-local, not reversed).
struct TextPathLayoutInput {
    std::string text;
    float fontSize = 48.0f;
    TextAlign align = TextAlign::Left;
    std::string fontFamily;
    std::string fontStyle;
    BezierPath path;
    bool reversed = false;
    bool perpendicular = true;
    bool forceAlignment = false;
    float firstMargin = 0.0f;
    float lastMargin = 0.0f;
};

// One glyph after path mapping, in text-local space.
struct TextPathGlyph {
    std::string utf8;
    Mat3 matrix;
    float advance = 0.0f;
};

struct TextPathLayoutResult {
    std::vector<TextPathGlyph> glyphs;
    Vec2 boundsMin;
    Vec2 boundsMax;
};

// Lays out point text along path using tgfx PathMeasure (PAG TextPathRender-aligned).
TextPathLayoutResult LayoutTextOnPath(const TextPathLayoutInput &input);

}  // namespace motion
