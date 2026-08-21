#include "MotionStudio/common/GeometryRevision.h"

#include <atomic>

namespace motion {

namespace {

uint64_t GenerateGeometryRevision() {
    static std::atomic<uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace

uint64_t GeometryRevisionAccess::Get(const VectorNetwork &network) {
    return network.revision_;
}

void GeometryRevisionAccess::Stamp(VectorNetwork &network) {
    network.revision_ = GenerateGeometryRevision();
}

uint64_t GeometryRevisionAccess::Get(const BezierPath &path) {
    return path.revision_;
}

void GeometryRevisionAccess::Stamp(BezierPath &path) {
    path.revision_ = GenerateGeometryRevision();
}

}  // namespace motion
