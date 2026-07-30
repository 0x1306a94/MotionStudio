#pragma once

#include <memory>
#include <string>

#include <tgfx/core/Typeface.h>

namespace motion {

// Resolves a typeface via public Typeface::MakeFromName(family, style),
// then PingFang SC, then Helvetica.
std::shared_ptr<tgfx::Typeface> ResolveTextTypeface(const std::string &fontFamily,
                                                    const std::string &fontStyle);

}  // namespace motion
