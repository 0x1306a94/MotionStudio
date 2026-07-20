#include "MotionStudio/undo/CompositeCommand.h"

#include <utility>

namespace motion {

CompositeCommand::CompositeCommand(std::string description)
    : description_(std::move(description)) {}

void CompositeCommand::add(std::unique_ptr<Command> command) {
    commands_.push_back(std::move(command));
}

size_t CompositeCommand::size() const {
    return commands_.size();
}

void CompositeCommand::execute(Document& document) {
    for (auto& command : commands_) {
        command->execute(document);
    }
}

void CompositeCommand::undo(Document& document) {
    for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) {
        (*it)->undo(document);
    }
}

std::string CompositeCommand::describe() const {
    return description_;
}

}  // namespace motion
