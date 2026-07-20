#include "MotionStudio/serialization/SchemaMigrator.h"

#include <nlohmann/json.hpp>

namespace motion {

Expected<std::string> SchemaMigrator::migrate(const std::string& jsonText) {
    try {
        // nlohmann 结构性解析错误在此边界统一转换为 Error。
        nlohmann::json data = nlohmann::json::parse(jsonText);
        const int fromVersion = data.at("schemaVersion").get<int>();
        if (fromVersion < 1 || fromVersion > currentVersion()) {
            return Error("不支持的 schemaVersion: " + std::to_string(fromVersion));
        }
        // 未来版本在此追加步骤：if (fromVersion == 1) data = migrateV1ToV2(std::move(data)); ...
        return data.dump();
    } catch (const std::exception& error) {
        return Error(std::string("文档 JSON 解析失败: ") + error.what());
    }
}

}  // namespace motion
