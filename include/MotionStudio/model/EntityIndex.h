#pragma once

#include <unordered_map>

#include "MotionStudio/common/EntityId.h"

namespace motion {

class Composition;
class Layer;
class ShapeElement;

// ID → 实体的全局扁平索引，O(1) 寻址。
// 供 undo 命令与桥接层解析目标：命令只持 EntityId，实体已删除则解析为 nullptr。
class EntityIndex {
public:
    Layer* findLayer(EntityId id) const;
    Composition* findComposition(EntityId id) const;
    ShapeElement* findShape(EntityId id) const;

    void registerLayer(Layer* layer);
    void registerComposition(Composition* composition);
    void registerShape(ShapeElement* shape);
    void clear();

private:
    std::unordered_map<EntityId, Layer*> layers_;
    std::unordered_map<EntityId, Composition*> compositions_;
    std::unordered_map<EntityId, ShapeElement*> shapes_;
};

}  // namespace motion
