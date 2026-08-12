#include "MeasurePointTextSize.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>
#include <unordered_map>
#include <utility>

#include "MotionStudio/textlayout/TextLayout.h"
#include "TgfxGlyphMetrics.h"
#include "TgfxTextTypeface.h"

namespace motion {

namespace {

uint32_t FloatBits(float value) {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "float must be 32-bit");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

struct MeasureCacheKey {
    std::string text = {};
    std::string fontFamily = {};
    std::string fontStyle = {};
    uint32_t fontSizeBits = 0;
    TextAlign align = TextAlign::Left;

    bool operator==(const MeasureCacheKey &other) const {
        return fontSizeBits == other.fontSizeBits && align == other.align && text == other.text &&
            fontFamily == other.fontFamily && fontStyle == other.fontStyle;
    }
};

struct MeasureCacheKeyHash {
    size_t operator()(const MeasureCacheKey &key) const {
        size_t hash = std::hash<std::string>{}(key.text);
        const size_t familyHash = std::hash<std::string>{}(key.fontFamily);
        hash ^= familyHash + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        const size_t styleHash = std::hash<std::string>{}(key.fontStyle);
        hash ^= styleHash + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<uint32_t>{}(key.fontSizeBits) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<int>{}(static_cast<int>(key.align)) + 0x9e3779b9 + (hash << 6) +
            (hash >> 2);
        return hash;
    }
};

// Process-local LRU: ResolvePointTextContainerSizes remasures every preview frame.
class PointTextSizeCache {
  public:
    static constexpr size_t Capacity = 256;

    bool find(const MeasureCacheKey &key, Vec2 &outSize) {
        const auto it = index_.find(key);
        if (it == index_.end()) {
            return false;
        }
        order_.splice(order_.begin(), order_, it->second);
        outSize = it->second->second;
        return true;
    }

    void put(MeasureCacheKey key, Vec2 size) {
        const auto it = index_.find(key);
        if (it != index_.end()) {
            it->second->second = size;
            order_.splice(order_.begin(), order_, it->second);
            return;
        }
        order_.emplace_front(std::move(key), size);
        index_[order_.front().first] = order_.begin();
        while (order_.size() > Capacity) {
            index_.erase(order_.back().first);
            order_.pop_back();
        }
    }

  private:
    using Entry = std::pair<MeasureCacheKey, Vec2>;
    std::list<Entry> order_ = {};
    std::unordered_map<MeasureCacheKey, std::list<Entry>::iterator, MeasureCacheKeyHash> index_ =
        {};
};

PointTextSizeCache &SharedPointTextSizeCache() {
    static PointTextSizeCache cache;
    return cache;
}

Vec2 MeasurePointTextSizeUncached(const std::string &text, float fontSize, TextAlign align,
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
        case TextAlign::Left: {
            input.align = textlayout::Align::Left;
            break;
        }
        case TextAlign::Center: {
            input.align = textlayout::Align::Center;
            break;
        }
        case TextAlign::Right: {
            input.align = textlayout::Align::Right;
            break;
        }
    }
    input.metrics = &glyphMetrics;
    const textlayout::TextLayoutResult layout = textlayout::LayoutText(input);
    return Vec2{std::max(1.0f, layout.measuredSize.x), std::max(1.0f, layout.measuredSize.y)};
}

}  // namespace

Vec2 MeasurePointTextSize(const std::string &text, float fontSize, TextAlign align,
                          const std::string &fontFamily, const std::string &fontStyle) {
    MeasureCacheKey key;
    key.text = text;
    key.fontFamily = fontFamily;
    key.fontStyle = fontStyle;
    key.fontSizeBits = FloatBits(fontSize);
    key.align = align;

    PointTextSizeCache &cache = SharedPointTextSizeCache();
    Vec2 cachedSize = {};
    if (cache.find(key, cachedSize)) {
        return cachedSize;
    }
    const Vec2 size = MeasurePointTextSizeUncached(text, fontSize, align, fontFamily, fontStyle);
    cache.put(std::move(key), size);
    return size;
}

}  // namespace motion
