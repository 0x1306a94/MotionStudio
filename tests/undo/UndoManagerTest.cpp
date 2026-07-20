#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/undo/CompositeCommand.h"
#include "MotionStudio/undo/UndoManager.h"

using motion::Command;
using motion::CommandKind;
using motion::CompositeCommand;
using motion::Document;
using motion::UndoManager;

namespace {

// 对计数器做加减的测试命令；mergeKey 非空且相同则可合并。
class AdjustCommand : public Command {
public:
    AdjustCommand(int* target, int delta, std::string mergeKey = {})
        : target_(target), delta_(delta), mergeKey_(std::move(mergeKey)) {}

    void execute(Document&) override { *target_ += delta_; }
    void undo(Document&) override { *target_ -= delta_; }

    // 测试专用命令：借用 Composite 作为类型标签，仅用于在本测试内识别同类命令
    // （本测试不会出现真正的 CompositeCommand）。
    CommandKind kind() const override { return CommandKind::Composite; }

    bool mergeWith(const Command& other) override {
        if (mergeKey_.empty()) {
            return false;
        }
        if (other.kind() != kind()) {
            return false;
        }
        const auto& typed = static_cast<const AdjustCommand&>(other);
        if (typed.mergeKey_ != mergeKey_) {
            return false;
        }
        delta_ += typed.delta_;
        return true;
    }

    std::string describe() const override { return "Adjust"; }

private:
    int* target_;
    int delta_;
    std::string mergeKey_;
};

// 记录执行顺序的命令。
class OrderCommand : public Command {
public:
    OrderCommand(std::vector<std::string>* log, std::string name)
        : log_(log), name_(std::move(name)) {}

    void execute(Document&) override { log_->push_back("do:" + name_); }
    void undo(Document&) override { log_->push_back("undo:" + name_); }
    CommandKind kind() const override { return CommandKind::Composite; }
    std::string describe() const override { return name_; }

private:
    std::vector<std::string>* log_;
    std::string name_;
};

}  // namespace

TEST(UndoManagerTest, ExecuteUndoRedoCycle) {
    Document document;
    UndoManager manager;
    int counter = 0;

    EXPECT_FALSE(manager.canUndo());
    manager.execute(document, std::make_unique<AdjustCommand>(&counter, 5));
    EXPECT_EQ(counter, 5);
    ASSERT_TRUE(manager.canUndo());
    EXPECT_EQ(manager.undoDescription(), "Adjust");

    manager.undo(document);
    EXPECT_EQ(counter, 0);
    ASSERT_TRUE(manager.canRedo());
    EXPECT_EQ(manager.redoDescription(), "Adjust");

    manager.redo(document);
    EXPECT_EQ(counter, 5);
    EXPECT_FALSE(manager.canRedo());
}

TEST(UndoManagerTest, NewExecuteClearsRedoStack) {
    Document document;
    UndoManager manager;
    int counter = 0;

    manager.execute(document, std::make_unique<AdjustCommand>(&counter, 1));
    manager.undo(document);
    ASSERT_TRUE(manager.canRedo());

    manager.execute(document, std::make_unique<AdjustCommand>(&counter, 10));
    EXPECT_FALSE(manager.canRedo());
    EXPECT_EQ(counter, 10);
}

TEST(UndoManagerTest, MaxHistoryDropsOldest) {
    Document document;
    UndoManager manager(/*maxHistory=*/3);
    int counter = 0;

    for (int i = 0; i < 5; ++i) {
        manager.execute(document, std::make_unique<AdjustCommand>(&counter, 1));
    }
    EXPECT_EQ(counter, 5);

    int undoCount = 0;
    while (manager.canUndo()) {
        manager.undo(document);
        ++undoCount;
    }
    EXPECT_EQ(undoCount, 3);
    EXPECT_EQ(counter, 2);  // 最旧的两次已被丢弃
}

TEST(UndoManagerTest, ConsecutiveCommandsMergeWithinWindow) {
    Document document;
    UndoManager manager;
    int counter = 0;

    manager.execute(document, std::make_unique<AdjustCommand>(&counter, 1, "drag"));
    manager.execute(document, std::make_unique<AdjustCommand>(&counter, 2, "drag"));
    manager.execute(document, std::make_unique<AdjustCommand>(&counter, 3, "drag"));
    EXPECT_EQ(counter, 6);

    manager.undo(document);  // 一个 undo 单元回退全部
    EXPECT_EQ(counter, 0);
    EXPECT_FALSE(manager.canUndo());
}

TEST(UndoManagerTest, DifferentMergeKeysDoNotMerge) {
    Document document;
    UndoManager manager;
    int counter = 0;

    manager.execute(document, std::make_unique<AdjustCommand>(&counter, 1, "a"));
    manager.execute(document, std::make_unique<AdjustCommand>(&counter, 1, "b"));

    manager.undo(document);
    EXPECT_EQ(counter, 1);
    ASSERT_TRUE(manager.canUndo());
    manager.undo(document);
    EXPECT_EQ(counter, 0);
}

TEST(UndoManagerTest, EndMergeGroupPreventsFurtherMerging) {
    Document document;
    UndoManager manager;
    int counter = 0;

    manager.execute(document, std::make_unique<AdjustCommand>(&counter, 1, "drag"));
    manager.endMergeGroup();
    manager.execute(document, std::make_unique<AdjustCommand>(&counter, 1, "drag"));

    manager.undo(document);
    EXPECT_EQ(counter, 1);
}

TEST(UndoManagerTest, MergeWindowExpiryPreventsMerging) {
    Document document;
    UndoManager manager(/*maxHistory=*/200, std::chrono::milliseconds(20));
    int counter = 0;

    manager.execute(document, std::make_unique<AdjustCommand>(&counter, 1, "drag"));
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    manager.execute(document, std::make_unique<AdjustCommand>(&counter, 1, "drag"));

    manager.undo(document);
    EXPECT_EQ(counter, 1);  // 未合并，只回退第二次
}

TEST(UndoManagerTest, UndoBreaksMergeWindow) {
    Document document;
    UndoManager manager;
    int counter = 0;

    manager.execute(document, std::make_unique<AdjustCommand>(&counter, 1, "drag"));
    manager.undo(document);
    manager.redo(document);
    // redo 后的新命令不应与栈顶合并。
    manager.execute(document, std::make_unique<AdjustCommand>(&counter, 1, "drag"));

    manager.undo(document);
    EXPECT_EQ(counter, 1);
}

TEST(CompositeCommandTest, ExecutesInOrderUndoesInReverse) {
    Document document;
    std::vector<std::string> log;

    auto composite = std::make_unique<CompositeCommand>("Batch");
    composite->add(std::make_unique<OrderCommand>(&log, "a"));
    composite->add(std::make_unique<OrderCommand>(&log, "b"));
    EXPECT_EQ(composite->size(), 2u);

    UndoManager manager;
    manager.execute(document, std::move(composite));
    manager.undo(document);

    const std::vector<std::string> expected = {"do:a", "do:b", "undo:b", "undo:a"};
    EXPECT_EQ(log, expected);
}

TEST(UndoManagerTest, ClearEmptiesBothStacks) {
    Document document;
    UndoManager manager;
    int counter = 0;

    manager.execute(document, std::make_unique<AdjustCommand>(&counter, 1));
    manager.undo(document);
    manager.clear();
    EXPECT_FALSE(manager.canUndo());
    EXPECT_FALSE(manager.canRedo());
}
