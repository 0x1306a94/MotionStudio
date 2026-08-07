#pragma once

#include <optional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/ShaderDefinition.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Removes a document shader when it is not referenced by any Fill/Stroke.
// If ShaderIsReferenced, execute skips deletion (shaders unchanged).
class RemoveShaderCommand : public Command {
  public:
    // shaderId: document shader to remove.
    explicit RemoveShaderCommand(EntityId shaderId);

    void execute(Document &document) override;
    void undo(Document &document) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    EntityId shaderId_ = {};
    std::optional<ShaderDefinition> removedShader_;
    int index_ = -1;
};

}  // namespace motion
