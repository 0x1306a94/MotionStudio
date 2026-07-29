#pragma once

#include <chrono>

namespace motion {

using TgfxProfileClock = std::chrono::steady_clock;

inline double Milliseconds(TgfxProfileClock::time_point start, TgfxProfileClock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

}  // namespace motion
