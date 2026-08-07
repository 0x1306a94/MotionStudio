#pragma once

#include <string>

#include "MotionStudio/model/ShaderDefinition.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Adds a document-level shader definition. Undo removes it from Document::shaders.
class AddShaderCommand : public Command {
  public:
    // shader: definition to insert; id must be unique in the document.
    explicit AddShaderCommand(ShaderDefinition shader);

    void execute(Document &document) override;
    void undo(Document &document) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    ShaderDefinition shader_ = {};
    bool inserted_ = false;
};

}  // namespace motion
