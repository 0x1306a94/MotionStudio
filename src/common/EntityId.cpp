#include "MotionStudio/common/EntityId.h"

#include <random>

namespace motion {

EntityId EntityId::generate() {
    static thread_local std::mt19937_64 engine{std::random_device{}()};
    uint64_t value = 0;
    while (value == 0) {
        value = engine();
    }
    return EntityId{value};
}

}  // namespace motion
