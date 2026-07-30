#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/TextAlign.h"

namespace motion {

// Evaluated text payload for one visible text layer at a point in time.
// Layout (wrap / shrink) is not performed here — only raw model values.
struct EvaluatedTextItem {
    std::string text;
    float fontSize = 48.0f;
    Vec2 containerSize;
    bool autoHeight = true;
    TextAlign align = TextAlign::Left;
    EntityId fontAssetId;
    std::string fontFamily;
    // Absolute font file path; empty when unbound or projectRoot/path missing.
    std::string fontAbsolutePath;
    Color fillColor{0, 0, 0, 1};
    std::optional<Color> strokeColor;
    float strokeWidth = 0.0f;
    // Core sets this to containerSize; Bridge/Canvas may overwrite y after TextLayout.
    Vec2 hitSize;
};

}  // namespace motion
