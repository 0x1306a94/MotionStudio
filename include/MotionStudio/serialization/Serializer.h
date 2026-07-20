#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace motion {

class Document;

// 模型 ↔ JSON v1（schema 见 docs/data-model.md §6，字段 camelCase）。
// 反序列化失败（格式错误 / 未知枚举 / 版本不支持）抛 std::invalid_argument。
class Serializer {
public:
    static std::string serialize(const Document& document);  // 缩进格式，写文件用
    static std::unique_ptr<Document> deserialize(const std::string& jsonText);
};

// 序列化结果的 FNV-1a 哈希。debug 测试用：undo 前后一致性断言。
uint64_t documentFingerprint(const Document& document);

}  // namespace motion
