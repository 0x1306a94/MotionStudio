#include "MeasurePointTextSize.h"

#include <algorithm>
#include <memory>

#include "MotionStudio/textlayout/TextLayout.h"
#include "TgfxGlyphMetrics.h"
#include "TgfxTextTypeface.h"

namespace motion {

Vec2 MeasurePointTextSize(const std::string &text, float fontSize, TextAlign align,
                          const std::string &fontFamily, const std::string &fontStyle) {
    std::shared_ptr<tgfx::Typeface> typeface = ResolveTextTypeface(fontFamily, fontStyle);
    if (typeface == nullptr) {
        return Vec2{1.0f, 1.0f};
    }

    TgfxGlyphMetrics glyphMetrics(typeface);
    textlayout::TextLayoutInput input;
    input.text = text;
    input.boxWidth = 1.0f;
    input.boxHeight = 1.0f;
    input.softWrap = false;
    input.shrinkToFit = false;
    input.fontSize = fontSize > 0.0f ? fontSize : 1.0f;
    switch (align) {
        case TextAlign::Left:
            input.align = textlayout::Align::Left;
            break;
        case TextAlign::Center:
            input.align = textlayout::Align::Center;
            break;
        case TextAlign::Right:
            input.align = textlayout::Align::Right;
            break;
    }
    input.metrics = &glyphMetrics;
    const textlayout::TextLayoutResult layout = textlayout::LayoutText(input);
    return Vec2{std::max(1.0f, layout.measuredSize.x), std::max(1.0f, layout.measuredSize.y)};
}

}  // namespace motion
