#include "TgfxTextTypeface.h"

#include <tgfx/core/FontStyle.h>

namespace motion {

namespace {

std::shared_ptr<tgfx::Typeface> MakeFromFamily(const std::string &family) {
    if (family.empty()) {
        return nullptr;
    }
    return tgfx::Typeface::MakeFromName(family, tgfx::FontStyle());
}

}  // namespace

std::shared_ptr<tgfx::Typeface> ResolveTextTypeface(const std::string &fontAbsolutePath,
                                                    const std::string &fontFamily) {
    if (!fontAbsolutePath.empty()) {
        if (auto typeface = tgfx::Typeface::MakeFromPath(fontAbsolutePath)) {
            return typeface;
        }
    }
    if (auto typeface = MakeFromFamily(fontFamily)) {
        return typeface;
    }
    if (auto typeface = MakeFromFamily("PingFang SC")) {
        return typeface;
    }
    return MakeFromFamily("Helvetica");
}

}  // namespace motion
