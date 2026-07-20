#pragma once

#include <vector>

#include "MotionStudio/common/Math.h"

namespace motion {

// 贝塞尔路径。切线为相对顶点的偏移量（Lottie 约定）：
// 控制点 = point + outTangent（出）、下一顶点 point + inTangent（入）。
struct BezierPath {
    struct Vertex {
        Vec2 point;
        Vec2 inTangent;
        Vec2 outTangent;

        bool operator==(const Vertex& other) const {
            return point == other.point && inTangent == other.inTangent &&
                   outTangent == other.outTangent;
        }
        bool operator!=(const Vertex& other) const { return !(*this == other); }
    };

    std::vector<Vertex> vertices;
    bool closed = false;

    bool operator==(const BezierPath& other) const {
        return vertices == other.vertices && closed == other.closed;
    }
    bool operator!=(const BezierPath& other) const { return !(*this == other); }
};

}  // namespace motion
