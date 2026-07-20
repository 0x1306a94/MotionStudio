#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace motion {

// 实体唯一标识。0 表示无效 ID。
// 命令与引用一律通过 EntityId 寻址，不持有裸指针。
struct EntityId {
    uint64_t value = 0;

    bool operator==(const EntityId& other) const;
    bool operator!=(const EntityId& other) const;

    bool isValid() const;

    // 生成随机 ID（安全随机数源，不会返回无效值）。
    static EntityId Generate();
};

}  // namespace motion

template <>
struct std::hash<motion::EntityId> {
    size_t operator()(const motion::EntityId& id) const noexcept {
        return std::hash<uint64_t>{}(id.value);
    }
};
