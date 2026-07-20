#include "MotionStudio/undo/Command.h"

#include <utility>

#include "MotionStudio/model/Document.h"

namespace motion {

CompositeCommand::CompositeCommand(std::string description)
    : description_(std::move(description)) {}

void CompositeCommand::add(std::unique_ptr<Command> command) {
    commands_.push_back(std::move(command));
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

}  // namespace motion
