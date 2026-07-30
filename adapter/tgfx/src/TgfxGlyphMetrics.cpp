#include "TgfxGlyphMetrics.h"

#include <cmath>

#include <tgfx/core/Font.h>

namespace motion {

TgfxGlyphMetrics::TgfxGlyphMetrics(std::shared_ptr<tgfx::Typeface> typeface)
    : typeface_(std::move(typeface)) {
}

textlayout::FontMetrics TgfxGlyphMetrics::metrics(float fontSize) const {
    textlayout::FontMetrics result;
    if (typeface_ == nullptr) {
        return result;
    }
    const tgfx::Font font(typeface_, fontSize);
    const tgfx::FontMetrics native = font.getMetrics();
    // Normalize to positive distances used by TextLayout (above / below baseline).
    result.ascent = std::fabs(native.ascent);
    result.descent = std::fabs(native.descent);
    result.leading = native.leading > 0.0f ? native.leading : 0.0f;
    return result;
}

float TgfxGlyphMetrics::advance(uint32_t unichar, float fontSize) const {
    if (typeface_ == nullptr) {
        return 0.0f;
    }
    const tgfx::Font font(typeface_, fontSize);
    const tgfx::GlyphID glyphId = font.getGlyphID(static_cast<tgfx::Unichar>(unichar));
    if (glyphId == 0) {
        return fontSize * 0.5f;
    }
    return font.getAdvance(glyphId);
}

}  // namespace motion
