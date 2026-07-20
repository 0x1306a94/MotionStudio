#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "MotionStudio/undo/Command.h"

namespace motion {

class Document;

// 双栈 undo/redo + 合并窗口。
// execute 行为：执行命令 → 若栈顶命令与之间隔在合并窗口内且 mergeWith 成功则吸收
// （不压栈，典型场景：拖拽）→ 否则压 undo 栈并清空 redo 栈 → 超 maxHistory 丢弃最旧。
class UndoManager {
public:
    explicit UndoManager(size_t maxHistory = 200,
                         std::chrono::milliseconds mergeWindow =
                             std::chrono::milliseconds(500));

    void execute(Document& document, std::unique_ptr<Command> command);
    void undo(Document& document);
    void redo(Document& document);

    bool canUndo() const;
    bool canRedo() const;
    std::string undoDescription() const;
    std::string redoDescription() const;

    void endMergeGroup();  // 鼠标抬起时调用，关闭合并窗口
    void clear();          // 打开/新建文档时清空历史（undo 历史不持久化）

private:
    std::deque<std::unique_ptr<Command>> undoStack_;
    std::vector<std::unique_ptr<Command>> redoStack_;
    size_t maxHistory_;
    std::chrono::milliseconds mergeWindow_;
    bool mergeWindowOpen_ = false;
    std::chrono::steady_clock::time_point lastExecuteTime_{};
};

}  // namespace motion
