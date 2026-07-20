#include "MotionStudio/serialization/SchemaMigrator.h"

#include <stdexcept>

#include <nlohmann/json.hpp>

namespace motion {

std::string SchemaMigrator::migrate(const std::string& jsonText) {
    nlohmann::json data;
    try {
        data = nlohmann::json::parse(jsonText);
        const int fromVersion = data.at("schemaVersion").get<int>();
        if (fromVersion < 1 || fromVersion > currentVersion()) {
            throw std::invalid_argument("不支持的 schemaVersion: " +
                                        std::to_string(fromVersion));
        }
    } catch (const nlohmann::json::exception& error) {
        throw std::invalid_argument(std::string("文档 JSON 解析失败: ") + error.what());
    }
    // 未来版本在此追加步骤：if (fromVersion == 1) data = migrateV1ToV2(std::move(data)); ...
    return data.dump();
}

}  // namespace motion
