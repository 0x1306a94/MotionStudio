#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace motion {

// Unique entity identifier. 0 represents an invalid ID.
// Commands and references always address entities via EntityId; raw pointers are never held.
struct EntityId {
    uint64_t value = 0;

    bool operator==(const EntityId &other) const;
    bool operator!=(const EntityId &other) const;

    // Returns true if this ID is not the invalid sentinel (0).
    bool isValid() const;

    // Generates a random ID from a secure random source; never returns an invalid value.
    static EntityId Generate();
};

}  // namespace motion

template <>
struct std::hash<motion::EntityId> {
    size_t operator()(const motion::EntityId &id) const noexcept {
        return std::hash<uint64_t>{}(id.value);
    }
};
