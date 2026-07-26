#pragma once

#include <memory>
#include <string>
#include <vector>

#include "MotionStudio/undo/Command.h"

namespace motion {

// Composite command: groups multiple atomic operations into a single undo unit.
// execute() runs children in order; undo() runs them in reverse.
class CompositeCommand : public Command {
  public:
    // description: human-readable label shown in the undo history.
    explicit CompositeCommand(std::string description);

    // Appends a child command.
    // command: takes ownership of the command.
    void add(std::unique_ptr<Command> command);
    // Returns the number of child commands.
    size_t size() const;

    void execute(Document &document) override;
    void undo(Document &document) override;
    bool mergeWith(const Command &other) override;
    CompositeCommand *asComposite() override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    std::string description_;
    std::vector<std::unique_ptr<Command>> commands_;
};

}  // namespace motion
