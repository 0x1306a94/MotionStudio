#pragma once

#include <cmath>
#include <cstdint>

namespace motion {

// 帧号，内部规范时间表示。整数帧号避免浮点漂移，序列化确定可读。
using FrameTime = int64_t;

// 左闭右开时间区间 [start, end)。
struct TimeRange {
    FrameTime start = 0;
    FrameTime end = 0;

    bool contains(FrameTime time) const { return time >= start && time < end; }

    bool operator==(const TimeRange& other) const {
        return start == other.start && end == other.end;
    }
    bool operator!=(const TimeRange& other) const { return !(*this == other); }
};

// 帧率 = num / den。覆盖非整数帧率（NTSC 29.97 = 30000/1001）。
struct FrameRate {
    uint32_t num = 30;
    uint32_t den = 1;

    double toSeconds(FrameTime frame) const { return double(frame) * den / num; }

    FrameTime fromSeconds(double seconds) const {
        return FrameTime(std::llround(seconds * num / den));
    }

    bool operator==(const FrameRate& other) const {
        return num == other.num && den == other.den;
    }
    bool operator!=(const FrameRate& other) const { return !(*this == other); }
};

}  // namespace motion
