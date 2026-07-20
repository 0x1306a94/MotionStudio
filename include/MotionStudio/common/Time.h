#pragma once

#include <cstdint>

namespace motion {

// 帧号，内部规范时间表示。整数帧号避免浮点漂移，序列化确定可读。
using FrameTime = int64_t;

// 左闭右开时间区间 [start, end)。
struct TimeRange {
    FrameTime start = 0;
    FrameTime end = 0;

    bool contains(FrameTime time) const;

    bool operator==(const TimeRange& other) const;
    bool operator!=(const TimeRange& other) const;
};

// 帧率 = num / den。覆盖非整数帧率（NTSC 29.97 = 30000/1001）。
struct FrameRate {
    uint32_t num = 30;
    uint32_t den = 1;

    double toSeconds(FrameTime frame) const;
    FrameTime fromSeconds(double seconds) const;

    bool operator==(const FrameRate& other) const;
    bool operator!=(const FrameRate& other) const;
};

}  // namespace motion
