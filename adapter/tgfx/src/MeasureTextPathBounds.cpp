#include "MeasureTextPathBounds.h"

#include "TextPathLayout.h"

namespace motion {

TextPathBounds MeasureTextPathBounds(const std::string &text, float fontSize, TextAlign align,
                                     const std::string &fontFamily, const std::string &fontStyle,
                                     const BezierPath &path, bool reversed, bool perpendicular,
                                     bool forceAlignment, float firstMargin, float lastMargin) {
    TextPathLayoutInput input;
    input.text = text;
    input.fontSize = fontSize;
    input.align = align;
    input.fontFamily = fontFamily;
    input.fontStyle = fontStyle;
    input.path = path;
    input.reversed = reversed;
    input.perpendicular = perpendicular;
    input.forceAlignment = forceAlignment;
    input.firstMargin = firstMargin;
    input.lastMargin = lastMargin;
    const TextPathLayoutResult layout = LayoutTextOnPath(input);
    TextPathBounds bounds;
    bounds.min = layout.boundsMin;
    bounds.max = layout.boundsMax;
    return bounds;
}

}  // namespace motion
