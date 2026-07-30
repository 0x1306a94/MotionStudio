#include "TgfxTextTypeface.h"

#include <tgfx/core/FontStyle.h>

namespace motion {

namespace {

// MakeFromName always returns some font (often Helvetica). Accept only exact family.
std::shared_ptr<tgfx::Typeface> MakeNamed(const std::string &family, const std::string &style) {
    if (family.empty()) {
        return nullptr;
    }
    if (!style.empty()) {
        if (auto typeface = tgfx::Typeface::MakeFromName(family, style)) {
            if (typeface->fontFamily() == family) {
                return typeface;
            }
        }
    }
    if (auto typeface = tgfx::Typeface::MakeFromName(family, tgfx::FontStyle())) {
        if (typeface->fontFamily() == family) {
            return typeface;
        }
    }
    return nullptr;
}

}  // namespace

std::shared_ptr<tgfx::Typeface> ResolveTextTypeface(const std::string &fontFamily,
                                                    const std::string &fontStyle) {
    if (auto typeface = MakeNamed(fontFamily, fontStyle)) {
        return typeface;
    }
    if (auto typeface = MakeNamed("PingFang SC", "")) {
        return typeface;
    }
    return tgfx::Typeface::MakeFromName("Helvetica", tgfx::FontStyle());
}

}  // namespace motion
