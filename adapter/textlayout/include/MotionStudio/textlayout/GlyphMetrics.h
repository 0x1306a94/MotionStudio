#pragma once

#include <cstdint>

namespace motion::textlayout {

struct FontMetrics {
    float ascent = 0;
    float descent = 0;
    float leading = 0;
};

// Provides per-size glyph advances and font metrics for layout.
class GlyphMetrics {
  public:
    virtual ~GlyphMetrics() = default;

    // fontSize: text size in pixels.
    virtual FontMetrics metrics(float fontSize) const = 0;

    // unichar: Unicode code point.
    // fontSize: text size in pixels.
    virtual float advance(uint32_t unichar, float fontSize) const = 0;
};

}  // namespace motion::textlayout
