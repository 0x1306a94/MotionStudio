#include "MotionStudio/undo/AddShaderCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ShaderUniformValues.h"

namespace motion {

AddShaderCommand::AddShaderCommand(ShaderDefinition shader)
    : shader_(std::move(shader)) {
}

void AddShaderCommand::execute(Document &document) {
    if (!shader_.id.isValid()) {
        return;
    }
    if (FindShader(document, shader_.id) != nullptr) {
        return;
    }
    document.shaders.push_back(shader_);
    inserted_ = true;
}

void AddShaderCommand::undo(Document &document) {
    if (!inserted_) {
        return;
    }
    for (auto it = document.shaders.begin(); it != document.shaders.end(); ++it) {
        if (it->id == shader_.id) {
            document.shaders.erase(it);
            break;
        }
    }
}

CommandKind AddShaderCommand::kind() const {
    return CommandKind::AddShader;
}

std::string AddShaderCommand::describe() const {
    return "Add Shader";
}

}  // namespace motion
