#include "MotionStudio/undo/UpdateShaderDefinitionCommand.h"

#include <utility>

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/StylePaintMode.h"

namespace motion {

UpdateShaderDefinitionCommand::UpdateShaderDefinitionCommand(
    EntityId shaderId, std::string name, std::string mainImage,
    std::vector<ShaderUniformDecl> uniforms)
    : shaderId_(shaderId)
    , name_(std::move(name))
    , mainImage_(std::move(mainImage))
    , uniforms_(std::move(uniforms)) {
}

void UpdateShaderDefinitionCommand::captureReferencingStyleSnapshots(Document &document) {
    styleSnapshots_.clear();
    for (const auto &composition : document.compositions) {
        if (composition == nullptr) {
            continue;
        }
        for (const auto &layer : composition->layers) {
            if (layer == nullptr) {
                continue;
            }
            for (size_t styleIndex = 0; styleIndex < layer->styles.size(); ++styleIndex) {
                LayerStyle *style = layer->styles[styleIndex].get();
                if (style == nullptr) {
                    continue;
                }
                if (style->type() == LayerStyleType::Fill) {
                    auto *fill = static_cast<FillStyle *>(style);
                    if (fill->paintMode != StylePaintMode::Shader || fill->shaderId != shaderId_) {
                        continue;
                    }
                    StyleUniformSnapshot snapshot;
                    snapshot.layerId = layer->id;
                    snapshot.styleIndex = static_cast<int>(styleIndex);
                    snapshot.uniformValues = fill->uniformValues;
                    styleSnapshots_.push_back(std::move(snapshot));
                } else if (style->type() == LayerStyleType::Stroke) {
                    auto *stroke = static_cast<StrokeStyle *>(style);
                    if (stroke->paintMode != StylePaintMode::Shader ||
                        stroke->shaderId != shaderId_) {
                        continue;
                    }
                    StyleUniformSnapshot snapshot;
                    snapshot.layerId = layer->id;
                    snapshot.styleIndex = static_cast<int>(styleIndex);
                    snapshot.uniformValues = stroke->uniformValues;
                    styleSnapshots_.push_back(std::move(snapshot));
                }
            }
        }
    }
}

void UpdateShaderDefinitionCommand::realignReferencingStyles(Document &document) const {
    for (const auto &composition : document.compositions) {
        if (composition == nullptr) {
            continue;
        }
        for (const auto &layer : composition->layers) {
            if (layer == nullptr) {
                continue;
            }
            for (const auto &stylePtr : layer->styles) {
                LayerStyle *style = stylePtr.get();
                if (style == nullptr) {
                    continue;
                }
                if (style->type() == LayerStyleType::Fill) {
                    auto *fill = static_cast<FillStyle *>(style);
                    if (fill->paintMode != StylePaintMode::Shader || fill->shaderId != shaderId_) {
                        continue;
                    }
                    Expected<ShaderUniformValues, std::string> realigned =
                        RealignUniformValues(uniforms_, fill->uniformValues);
                    if (realigned.hasValue()) {
                        fill->uniformValues = std::move(*realigned);
                    }
                } else if (style->type() == LayerStyleType::Stroke) {
                    auto *stroke = static_cast<StrokeStyle *>(style);
                    if (stroke->paintMode != StylePaintMode::Shader ||
                        stroke->shaderId != shaderId_) {
                        continue;
                    }
                    Expected<ShaderUniformValues, std::string> realigned =
                        RealignUniformValues(uniforms_, stroke->uniformValues);
                    if (realigned.hasValue()) {
                        stroke->uniformValues = std::move(*realigned);
                    }
                }
            }
        }
    }
}

void UpdateShaderDefinitionCommand::restoreStyleUniformSnapshots(Document &document) const {
    for (const StyleUniformSnapshot &snapshot : styleSnapshots_) {
        Layer *layer = document.entityIndex().findLayer(snapshot.layerId);
        if (layer == nullptr || snapshot.styleIndex < 0 ||
            static_cast<size_t>(snapshot.styleIndex) >= layer->styles.size()) {
            continue;
        }
        LayerStyle *style = layer->styles[static_cast<size_t>(snapshot.styleIndex)].get();
        if (style == nullptr) {
            continue;
        }
        if (style->type() == LayerStyleType::Fill) {
            static_cast<FillStyle *>(style)->uniformValues = snapshot.uniformValues;
        } else if (style->type() == LayerStyleType::Stroke) {
            static_cast<StrokeStyle *>(style)->uniformValues = snapshot.uniformValues;
        }
    }
}

void UpdateShaderDefinitionCommand::execute(Document &document) {
    ShaderDefinition *shader = FindShader(document, shaderId_);
    if (shader == nullptr) {
        return;
    }
    if (!captured_) {
        oldName_ = shader->name;
        oldMainImage_ = shader->mainImage;
        oldUniforms_ = shader->uniforms;
        captureReferencingStyleSnapshots(document);
        captured_ = true;
    }
    shader->name = name_;
    shader->mainImage = mainImage_;
    shader->uniforms = uniforms_;
    realignReferencingStyles(document);
}

void UpdateShaderDefinitionCommand::undo(Document &document) {
    if (!captured_) {
        return;
    }
    ShaderDefinition *shader = FindShader(document, shaderId_);
    if (shader == nullptr) {
        return;
    }
    shader->name = oldName_;
    shader->mainImage = oldMainImage_;
    shader->uniforms = oldUniforms_;
    restoreStyleUniformSnapshots(document);
}

CommandKind UpdateShaderDefinitionCommand::kind() const {
    return CommandKind::UpdateShaderDefinition;
}

std::string UpdateShaderDefinitionCommand::describe() const {
    return "Update Shader";
}

}  // namespace motion
