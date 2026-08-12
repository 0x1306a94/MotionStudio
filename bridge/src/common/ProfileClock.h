#pragma once

#include <chrono>

namespace bridge {
using ProfileClock = std::chrono::steady_clock;

inline double Milliseconds(const ProfileClock::time_point &start, const ProfileClock::time_point &end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}
};  // namespace bridge