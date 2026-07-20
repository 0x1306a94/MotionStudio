#pragma once

#include <memory>
#include <string>
#include <vector>

#include "MotionStudio/undo/Command.h"

namespace motion {

// 复合命令：多原子操作作为一个 undo 单元。execute 顺序执行，undo 逆序执行。
class CompositeCommand : public Command {
public:
    explicit CompositeCommand(std::string description);

    void add(std::unique_ptr<Command> command);
    size_t size() const;

    void execute(Document& document) override;
    void undo(Document& document) override;
    CommandKind kind() const override;
    std::string describe() const override;

private:
    std::string description_;
    std::vector<std::unique_ptr<Command>> commands_;
};

}  // namespace motion
