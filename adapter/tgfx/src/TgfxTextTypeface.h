#pragma once

#include <memory>
#include <string>

#include <tgfx/core/Typeface.h>

namespace motion {

// Resolves a typeface for text drawing: family name, then PingFang SC, then Helvetica.
std::shared_ptr<tgfx::Typeface> ResolveTextTypeface(const std::string &fontFamily);

}  // namespace motion
