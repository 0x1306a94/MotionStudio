#pragma once

#include <optional>
#include <string>
#include <vector>

#include "MotionStudio/common/BezierPath.h"
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

// Path geometry for point text, already in text-local space (not reversed).
struct EvaluatedTextPath {
    BezierPath path;
    bool reversed = false;
    bool perpendicular = true;
    bool forceAlignment = false;
    float firstMargin = 0;
    float lastMargin = 0;
};

// Evaluated text payload for one visible text layer at a point in time.
// Layout (wrap / shrink / path) is not performed here — only raw model values
// plus optional path transformed into text local space.
struct EvaluatedTextItem {
    std::string text;
    float fontSize = 48.0f;
    Vec2 containerSize;
    bool boxTextMode = false;
    TextAlign align = TextAlign::Left;
    std::string fontFamily;
    std::string fontStyle;
    // Fill/stroke paints in Layer::styles order. Empty → adapter draws black fill.
    std::vector<TextDrawStyle> styles;
    // Present when TextPath resolves to a usable path in text-local space.
    std::optional<EvaluatedTextPath> textPath;
    // When true, AABB uses localBoundsMin/Max instead of origin..containerSize.
    bool useExactLocalBounds = false;
    Vec2 localBoundsMin{};
    Vec2 localBoundsMax{};
};

}  // namespace motion
