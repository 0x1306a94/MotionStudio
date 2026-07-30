#pragma once

#include <memory>

#include <tgfx/core/Typeface.h>

#include "MotionStudio/textlayout/GlyphMetrics.h"

namespace motion {

// GlyphMetrics backed by a tgfx Typeface (Font created per query size).
class TgfxGlyphMetrics : public textlayout::GlyphMetrics {
  public:
    explicit TgfxGlyphMetrics(std::shared_ptr<tgfx::Typeface> typeface);

    textlayout::FontMetrics metrics(float fontSize) const override;
    float advance(uint32_t unichar, float fontSize) const override;

  private:
    std::shared_ptr<tgfx::Typeface> typeface_;
};

}  // namespace motion
