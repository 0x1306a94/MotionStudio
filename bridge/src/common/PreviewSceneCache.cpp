#include "PreviewSceneCache.h"

#include <utility>

#include "PreviewTimeKey.h"

namespace motionstudio {

void PreviewSceneCache::clear() {
    entries_.clear();
    lru_.clear();
    lruPositions_.clear();
    compositionId_ = 0;
    revision_ = 0;
}

void PreviewSceneCache::invalidateIfStale(uint64_t compositionId, uint64_t revision) {
    if (compositionId_ == compositionId && revision_ == revision) {
        return;
    }
    clear();
    compositionId_ = compositionId;
    revision_ = revision;
}

const PreviewSceneCache::Entry *PreviewSceneCache::find(motion::PreviewTime time) {
    const uint64_t key = PreviewTimeKey(time);
    const auto found = entries_.find(key);
    if (found == entries_.end()) {
        return nullptr;
    }
    touch(key);
    return found->second.get();
}

PreviewSceneCache::Entry *PreviewSceneCache::put(motion::PreviewTime time,
                                                 std::unique_ptr<Entry> entry) {
    if (entry == nullptr) {
        return nullptr;
    }
    entry->time = time;
    const uint64_t key = PreviewTimeKey(time);
    auto found = entries_.find(key);
    if (found != entries_.end()) {
        found->second = std::move(entry);
        touch(key);
        return found->second.get();
    }
    Entry *stored = entry.get();
    entries_.emplace(key, std::move(entry));
    lru_.push_front(key);
    lruPositions_[key] = lru_.begin();
    evictIfNeeded();
    return stored;
}

void PreviewSceneCache::touch(uint64_t key) {
    const auto position = lruPositions_.find(key);
    if (position == lruPositions_.end()) {
        return;
    }
    lru_.erase(position->second);
    lru_.push_front(key);
    lruPositions_[key] = lru_.begin();
}

void PreviewSceneCache::evictIfNeeded() {
    while (entries_.size() > MaxEntries && !lru_.empty()) {
        const uint64_t oldest = lru_.back();
        lru_.pop_back();
        lruPositions_.erase(oldest);
        entries_.erase(oldest);
    }
}

}  // namespace motionstudio
