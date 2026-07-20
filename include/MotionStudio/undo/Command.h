#pragma once

#include <string>

#include "MotionStudio/undo/CommandKind.h"

namespace motion {

class Document;

// 命令模式：UI 的每次编辑对应一个命令。
// 命令只持 EntityId 不持指针，undo 时目标可能已被删除——经 EntityIndex 解析，
// 不存在则静默跳过。
class Command {
public:
    virtual ~Command() = default;

    virtual void execute(Document& document) = 0;  // 首次执行 + redo
    virtual void undo(Document& document) = 0;

    // Concrete type tag used by mergeWith to type-check `other` before
    // downcasting (dynamic_cast is banned).
    virtual CommandKind kind() const = 0;

    // 合并连续操作（如拖拽产生的几十次命令收敛为一个 undo 单元）。
    // 成功则吸收 other（修改自身状态），调用方丢弃 other。
    virtual bool mergeWith(const Command& /*other*/) { return false; }

    virtual std::string describe() const = 0;  // "Move Keyframe"
};

}  // namespace motion
