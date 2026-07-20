#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/Asset.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/ShapeElement.h"

namespace motion {

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

class Document {
public:
    // ---- 结构修改入口（调用后自动刷新 EntityIndex）----

    // 返回插入后的裸指针（所有权仍在 Document）；composition 为空返回 nullptr。
    Composition* addComposition(std::unique_ptr<Composition> composition);
    // 移走合成（含其全部图层）；不存在返回 nullptr。
    std::unique_ptr<Composition> takeComposition(EntityId id);

    // index < 0 或越界时追加到末尾。返回插入后的裸指针。
    Layer* addLayer(EntityId compositionId, std::unique_ptr<Layer> layer, int index = -1);
    // 移走图层；不存在返回 nullptr。
    std::unique_ptr<Layer> takeLayer(EntityId compositionId, EntityId layerId);
    // 调整图层顺序；索引越界返回 false。
    bool moveLayer(EntityId compositionId, int fromIndex, int toIndex);

    // ---- 查询 ----

    EntityIndex& entityIndex() { return entityIndex_; }
    const EntityIndex& entityIndex() const { return entityIndex_; }

    // 遍历整棵实体树重建索引。批量构造（如反序列化）后调用一次。
    void refreshEntityIndex();

    EntityId id = EntityId::generate();
    std::string name;
    std::vector<std::unique_ptr<Composition>> compositions;
    std::vector<Asset> assets;  // 图片/字体等文档级资源

private:
    EntityIndex entityIndex_;
};

}  // namespace motion
