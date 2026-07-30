#pragma once

#include <string>
#include <vector>

#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/textlayout/GlyphMetrics.h"

namespace motion::textlayout {

enum class Align {
    Left,
    Center,
    Right,
};

struct TextLayoutInput {
    std::string text;
    float boxWidth = 0;
    float boxHeight = 0;       // fixed layout box height
    bool shrinkToFit = false;  // true = binary-search font shrink into box
    float fontSize = 48;
    Align align = Align::Left;
    const GlyphMetrics *metrics = nullptr;
};

struct TextLine {
    std::string text;
    float x = 0;
    float y = 0;  // baseline y from the top of the box
    float width = 0;
};

struct TextLayoutResult {
    float appliedFontSize = 0;
    Vec2 measuredSize;
    std::vector<TextLine> lines;
};

// Lays out boxed text: hard breaks on '\\n', soft wrap by width; when
// shrinkToFit is true, binary-search font size to fit boxHeight.
TextLayoutResult LayoutText(const TextLayoutInput &input);

}  // namespace motion::textlayout
