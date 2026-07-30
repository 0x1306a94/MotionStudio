#include "TgfxImageCache.h"

namespace motion {

TgfxImageCache::TgfxImageCache(size_t capacity)
    : capacity_(capacity == 0 ? 1 : capacity) {
}

std::shared_ptr<tgfx::Image> TgfxImageCache::GetOrLoad(const std::string &absolutePath) {
    if (absolutePath.empty()) {
        return nullptr;
    }
    auto found = entries_.find(absolutePath);
    if (found != entries_.end()) {
        order_.splice(order_.begin(), order_, found->second.first);
        return found->second.second;
    }

    auto image = tgfx::Image::MakeFromFile(absolutePath);
    if (image == nullptr) {
        return nullptr;
    }
    order_.push_front(absolutePath);
    entries_.emplace(absolutePath, std::make_pair(order_.begin(), image));
    while (entries_.size() > capacity_) {
        const std::string &evict = order_.back();
        entries_.erase(evict);
        order_.pop_back();
    }
    return image;
}

void TgfxImageCache::Clear() {
    entries_.clear();
    order_.clear();
}

}  // namespace motion
