#include "MotionStudio/undo/RemoveShaderCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ShaderUniformValues.h"

namespace motion {

RemoveShaderCommand::RemoveShaderCommand(EntityId shaderId)
    : shaderId_(shaderId) {
}

void RemoveShaderCommand::execute(Document &document) {
    if (!shaderId_.isValid()) {
        return;
    }
    if (ShaderIsReferenced(document, shaderId_)) {
        return;
    }
    for (size_t index = 0; index < document.shaders.size(); ++index) {
        if (document.shaders[index].id == shaderId_) {
            removedShader_ = std::move(document.shaders[index]);
            index_ = static_cast<int>(index);
            document.shaders.erase(document.shaders.begin() + static_cast<ptrdiff_t>(index));
            return;
        }
    }
}

void RemoveShaderCommand::undo(Document &document) {
    if (!removedShader_) {
        return;
    }
    if (FindShader(document, shaderId_) != nullptr) {
        return;
    }
    const size_t insertIndex =
        index_ < 0 ? document.shaders.size()
        : static_cast<size_t>(index_) > document.shaders.size()
        ? document.shaders.size()
        : static_cast<size_t>(index_);
    document.shaders.insert(document.shaders.begin() + static_cast<ptrdiff_t>(insertIndex),
                            std::move(*removedShader_));
    removedShader_.reset();
}

CommandKind RemoveShaderCommand::kind() const {
    return CommandKind::RemoveShader;
}

std::string RemoveShaderCommand::describe() const {
    return "Remove Shader";
}

}  // namespace motion
