#pragma once

#include <memory>
#include <string>
#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/Asset.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/EntityIndex.h"

namespace motion {

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

    EntityIndex& entityIndex();
    const EntityIndex& entityIndex() const;

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
