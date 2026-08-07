#pragma once

#include <string>
#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/model/ShaderDefinition.h"
#include "MotionStudio/model/ShaderUniformValues.h"
#include "MotionStudio/undo/Command.h"

namespace motion {

// Updates a shader's name/mainImage/uniforms and realigns uniformValues on
// every Fill/Stroke that references it. Captures prior state for undo.
class UpdateShaderDefinitionCommand : public Command {
  public:
    // shaderId: target definition in Document.shaders.
    // name: new display name.
    // mainImage: new mainImage source body.
    // uniforms: new scheme declarations (triggers RealignUniformValues).
    UpdateShaderDefinitionCommand(EntityId shaderId, std::string name, std::string mainImage,
                                  std::vector<ShaderUniformDecl> uniforms);

    void execute(Document &document) override;
    void undo(Document &document) override;
    CommandKind kind() const override;
    std::string describe() const override;

  private:
    struct StyleUniformSnapshot {
        EntityId layerId = {};
        int styleIndex = -1;
        ShaderUniformValues uniformValues;
    };

    void captureReferencingStyleSnapshots(Document &document);
    void realignReferencingStyles(Document &document) const;
    void restoreStyleUniformSnapshots(Document &document) const;

    EntityId shaderId_ = {};
    std::string name_;
    std::string mainImage_;
    std::vector<ShaderUniformDecl> uniforms_;

    bool captured_ = false;
    std::string oldName_;
    std::string oldMainImage_;
    std::vector<ShaderUniformDecl> oldUniforms_;
    std::vector<StyleUniformSnapshot> styleSnapshots_;
};

}  // namespace motion
