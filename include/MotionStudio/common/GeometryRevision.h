#pragma once

#include <cstdint>

#include "MotionStudio/common/BezierPath.h"
#include "MotionStudio/common/VectorNetwork.h"

namespace motion {

// Cache identity for VectorNetwork / BezierPath. Not serialized. Not part of the
// authoring model — include this header only from compile / adapter / tests.
class GeometryRevisionAccess {
  public:
    static uint64_t Get(const VectorNetwork &network);
    static void Stamp(VectorNetwork &network);
    static uint64_t Get(const BezierPath &path);
    static void Stamp(BezierPath &path);
};

}  // namespace motion
