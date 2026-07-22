#pragma once

#include <cstdint>

namespace motion {

// Frame number — the canonical internal time representation.
// Integer frames avoid floating-point drift and serialize deterministically.
using FrameTime = int64_t;

// Fractional frame time for live preview evaluation. This is not serialized
// and does not replace FrameTime for keyframes, editing, or export.
using PreviewTime = double;

// Half-open time interval [start, end).
struct TimeRange {
    FrameTime start = 0;
    FrameTime end = 0;

    // Returns true if time falls within [start, end).
    // time: the frame time to test.
    bool contains(FrameTime time) const;

    bool operator==(const TimeRange &other) const;
    bool operator!=(const TimeRange &other) const;
};

// Frame rate expressed as num / den, supporting non-integer rates
// (e.g. NTSC 29.97 = 30000/1001).
struct FrameRate {
    uint32_t num = 30;
    uint32_t den = 1;

    // Converts a frame count to seconds.
    // frame: frame number to convert.
    double toSeconds(FrameTime frame) const;

    // Converts seconds to the nearest frame number.
    // seconds: duration in seconds to convert.
    FrameTime fromSeconds(double seconds) const;

    bool operator==(const FrameRate &other) const;
    bool operator!=(const FrameRate &other) const;
};

}  // namespace motion
