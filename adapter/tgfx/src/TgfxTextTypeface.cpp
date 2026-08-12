#include "TgfxTextTypeface.h"

#include <list>
#include <unordered_map>
#include <utility>

#include <tgfx/core/FontStyle.h>

namespace motion {

namespace {

struct TypefaceCacheKey {
    std::string fontFamily = {};
    std::string fontStyle = {};

    bool operator==(const TypefaceCacheKey &other) const {
        return fontFamily == other.fontFamily && fontStyle == other.fontStyle;
    }
};

struct TypefaceCacheKeyHash {
    size_t operator()(const TypefaceCacheKey &key) const {
        const size_t familyHash = std::hash<std::string>{}(key.fontFamily);
        const size_t styleHash = std::hash<std::string>{}(key.fontStyle);
        return familyHash ^ (styleHash + 0x9e3779b9 + (familyHash << 6) + (familyHash >> 2));
    }
};

// Process-local LRU: MakeFromName / CTFont matching dominated edit-drag hangs in
// Instruments. Preview draw and bridge text measure share the main DocumentLock path.
class TypefaceCache {
  public:
    static constexpr size_t Capacity = 64;

    std::shared_ptr<tgfx::Typeface> find(const TypefaceCacheKey &key) {
        const auto it = index_.find(key);
        if (it == index_.end()) {
            return nullptr;
        }
        order_.splice(order_.begin(), order_, it->second);
        return it->second->second;
    }

    void put(TypefaceCacheKey key, std::shared_ptr<tgfx::Typeface> typeface) {
        const auto it = index_.find(key);
        if (it != index_.end()) {
            it->second->second = std::move(typeface);
            order_.splice(order_.begin(), order_, it->second);
            return;
        }
        order_.emplace_front(std::move(key), std::move(typeface));
        index_[order_.front().first] = order_.begin();
        while (order_.size() > Capacity) {
            index_.erase(order_.back().first);
            order_.pop_back();
        }
    }

  private:
    using Entry = std::pair<TypefaceCacheKey, std::shared_ptr<tgfx::Typeface>>;
    std::list<Entry> order_ = {};
    std::unordered_map<TypefaceCacheKey, std::list<Entry>::iterator, TypefaceCacheKeyHash> index_ =
        {};
};

TypefaceCache &SharedTypefaceCache() {
    static TypefaceCache cache;
    return cache;
}

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

std::shared_ptr<tgfx::Typeface> ResolveTextTypefaceUncached(const std::string &fontFamily,
                                                            const std::string &fontStyle) {
    if (auto typeface = MakeNamed(fontFamily, fontStyle)) {
        return typeface;
    }
    if (auto typeface = MakeNamed("PingFang SC", "")) {
        return typeface;
    }
    return tgfx::Typeface::MakeFromName("Helvetica", tgfx::FontStyle());
}

}  // namespace

std::shared_ptr<tgfx::Typeface> ResolveTextTypeface(const std::string &fontFamily,
                                                    const std::string &fontStyle) {
    TypefaceCacheKey key;
    key.fontFamily = fontFamily;
    key.fontStyle = fontStyle;
    TypefaceCache &cache = SharedTypefaceCache();
    if (std::shared_ptr<tgfx::Typeface> hit = cache.find(key)) {
        return hit;
    }
    std::shared_ptr<tgfx::Typeface> typeface = ResolveTextTypefaceUncached(fontFamily, fontStyle);
    cache.put(std::move(key), typeface);
    return typeface;
}

}  // namespace motion
