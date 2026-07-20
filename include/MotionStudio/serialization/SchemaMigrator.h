#pragma once

#include <string>

namespace motion {

// 版本迁移链：v1 → v2 → ... → current，每步是纯函数 JSON → JSON。
// M1 只有 v1，骨架先行，新版本在此挂迁移步骤。
// 输入输出均为 JSON 文本，公共头不暴露第三方 JSON 类型。
class SchemaMigrator {
public:
    static constexpr int currentVersion() { return 1; }

    // 读取 schemaVersion 并迁移到当前版本；格式错误或版本不支持抛 std::invalid_argument。
    static std::string migrate(const std::string& jsonText);
};

}  // namespace motion
