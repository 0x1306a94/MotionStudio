#pragma once

#include <cstdint>
#include <list>
#include <unordered_map>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/render/DrawCommand.h"

namespace motionstudio {

// Caches scene DrawCommandList for playback integer frames so looping / repeated
// draws skip SceneEvaluator and BuildCommands. View transform and editor chrome
// stay outside this cache.
class FrameCommandCache {
  public:
    struct Entry {
        int viewportWidth = 0;
        int viewportHeight = 0;
        motion::Color backgroundColor = {};
        float cornerRadius = 0.0f;
        size_t layerCount = 0;
        motion::DrawCommandList commands = {};
    };

    void clear();
    void invalidateIfStale(uint64_t compositionId, uint64_t revision);
    const Entry *find(int64_t frame);
    void put(int64_t frame, Entry entry);

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
    std::unordered_map<int64_t, Entry> entries_ = {};
    std::list<int64_t> lru_ = {};
    std::unordered_map<int64_t, std::list<int64_t>::iterator> lruPositions_ = {};

    void touch(int64_t frame);
    void evictIfNeeded();
};

}  // namespace motionstudio
