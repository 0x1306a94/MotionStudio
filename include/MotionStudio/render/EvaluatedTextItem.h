#pragma once

#include <string>
#include <vector>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/TextAlign.h"

namespace motion {

// One fill or stroke pass for boxed text, in Layer::styles order.
struct TextDrawStyle {
    Color color{0, 0, 0, 1};
    BlendMode blendMode = BlendMode::Normal;
    bool isStroke = false;
    float strokeWidth = 0.0f;
};

// Evaluated text payload for one visible text layer at a point in time.
// Layout (wrap / shrink) is not performed here — only raw model values.
struct EvaluatedTextItem {
    std::string text;
    float fontSize = 48.0f;
    Vec2 containerSize;
    bool autoHeight = true;
    TextAlign align = TextAlign::Left;
    std::string fontFamily;
    // Fill/stroke paints in Layer::styles order. Empty → adapter draws black fill.
    std::vector<TextDrawStyle> styles;
    // Core sets this to containerSize; Bridge/Canvas may overwrite y after TextLayout.
    Vec2 hitSize;
};

}  // namespace motion
