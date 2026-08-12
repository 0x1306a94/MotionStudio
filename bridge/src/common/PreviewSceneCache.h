#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <unordered_map>

#include "MotionStudio/common/Time.h"
#include "MotionStudio/render/SceneState.h"

namespace motionstudio {

// Multi-frame LRU of evaluated SceneState for a single composition + content
// revision. Shared by draw, hit-test, bounds, and selection handles.
class PreviewSceneCache {
  public:
    struct Entry {
        motion::PreviewTime time = 0;
        motion::SceneState state;
    };

    void clear();
    void invalidateIfStale(uint64_t compositionId, uint64_t revision);
    const Entry *find(motion::PreviewTime time);
    // Takes ownership; overwrites same key. Returned pointer is owned by the
    // cache — do not delete; valid only while DocumentLock is held.
    Entry *put(motion::PreviewTime time, std::unique_ptr<Entry> entry);

    uint64_t compositionId() const {
        return compositionId_;
    }
    uint64_t revision() const {
        return revision_;
    }
    size_t size() const {
        return entries_.size();
    }

  private:
    static constexpr size_t MaxEntries = 256;

    uint64_t compositionId_ = 0;
    uint64_t revision_ = 0;
    std::unordered_map<uint64_t, std::unique_ptr<Entry>> entries_ = {};
    std::list<uint64_t> lru_ = {};
    std::unordered_map<uint64_t, std::list<uint64_t>::iterator> lruPositions_ = {};

    void touch(uint64_t key);
    void evictIfNeeded();
};

}  // namespace motionstudio
