#pragma once

#include <cstdint>
#include <cstring>

#include "MotionStudio/common/Time.h"

namespace motionstudio {

// Exact bit pattern of PreviewTime for cache keys (C++17: no std::bit_cast).
inline uint64_t PreviewTimeKey(motion::PreviewTime time) {
    static_assert(sizeof(motion::PreviewTime) == sizeof(uint64_t), "");
    uint64_t bits = 0;
    std::memcpy(&bits, &time, sizeof(bits));
    return bits;
}

}  // namespace motionstudio
