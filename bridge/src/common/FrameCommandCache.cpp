#include "FrameCommandCache.h"

#include <utility>

namespace motionstudio {

void FrameCommandCache::clear() {
    entries_.clear();
    lru_.clear();
    lruPositions_.clear();
    compositionId_ = 0;
    revision_ = 0;
}

void FrameCommandCache::invalidateIfStale(uint64_t compositionId, uint64_t revision) {
    if (compositionId_ == compositionId && revision_ == revision) {
        return;
    }
    clear();
    compositionId_ = compositionId;
    revision_ = revision;
}

const FrameCommandCache::Entry *FrameCommandCache::find(int64_t frame) {
    const auto found = entries_.find(frame);
    if (found == entries_.end()) {
        return nullptr;
    }
    touch(frame);
    return &found->second;
}

void FrameCommandCache::put(int64_t frame, Entry entry) {
    auto found = entries_.find(frame);
    if (found != entries_.end()) {
        found->second = std::move(entry);
        touch(frame);
        return;
    }
    entries_.emplace(frame, std::move(entry));
    lru_.push_front(frame);
    lruPositions_[frame] = lru_.begin();
    evictIfNeeded();
}

void FrameCommandCache::touch(int64_t frame) {
    const auto position = lruPositions_.find(frame);
    if (position == lruPositions_.end()) {
        return;
    }
    lru_.erase(position->second);
    lru_.push_front(frame);
    lruPositions_[frame] = lru_.begin();
}

void FrameCommandCache::evictIfNeeded() {
    while (entries_.size() > MaxEntries && !lru_.empty()) {
        const int64_t oldest = lru_.back();
        lru_.pop_back();
        lruPositions_.erase(oldest);
        entries_.erase(oldest);
    }
}

}  // namespace motionstudio
