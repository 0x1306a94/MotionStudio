#include "MotionStudio/common/Time.h"

#include <cmath>

namespace motion {

bool TimeRange::contains(FrameTime time) const {
    return time >= start && time < end;
}

bool TimeRange::operator==(const TimeRange &other) const {
    return start == other.start && end == other.end;
}

bool TimeRange::operator!=(const TimeRange &other) const {
    return !(*this == other);
}

double FrameRate::toSeconds(FrameTime frame) const {
    return double(frame) * den / num;
}

FrameTime FrameRate::fromSeconds(double seconds) const {
    return FrameTime(std::llround(seconds * num / den));
}

bool FrameRate::operator==(const FrameRate &other) const {
    return num == other.num && den == other.den;
}

bool FrameRate::operator!=(const FrameRate &other) const {
    return !(*this == other);
}

}  // namespace motion
