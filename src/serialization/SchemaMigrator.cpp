#include "MotionStudio/serialization/SchemaMigrator.h"

#include <nlohmann/json.hpp>

namespace motion {

Expected<std::string> SchemaMigrator::migrate(const std::string& jsonText) {
    // 关闭 nlohmann 解析异常，错误以 discarded 值返回。
    const nlohmann::json data = nlohmann::json::parse(jsonText, nullptr, false);
    if (data.is_discarded() || !data.is_object()) {
        return Error("文档 JSON 解析失败");
    }
    const auto it = data.find("schemaVersion");
    if (it == data.end() || !it->is_number_integer()) {
        return Error("缺少合法的 schemaVersion 字段");
    }
    const int fromVersion = it->get<int>();
    if (fromVersion < 1 || fromVersion > currentVersion()) {
        return Error("不支持的 schemaVersion: " + std::to_string(fromVersion));
    }
    // 未来版本在此追加步骤：if (fromVersion == 1) data = migrateV1ToV2(data); ...
    return data.dump();
}

}  // namespace motion
