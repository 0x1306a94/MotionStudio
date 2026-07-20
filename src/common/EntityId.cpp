#include "MotionStudio/common/EntityId.h"

#include <random>

namespace motion {

bool EntityId::operator==(const EntityId &other) const {
    return value == other.value;
}

bool EntityId::operator!=(const EntityId &other) const {
    return value != other.value;
}

bool EntityId::isValid() const {
    return value != 0;
}

EntityId EntityId::Generate() {
    static thread_local std::mt19937_64 engine{std::random_device{}()};
    uint64_t value = 0;
    while (value == 0) {
        value = engine();
    }
    return EntityId{value};
}

}  // namespace motion
