#include "motionstudio_bridge.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/GradientPaint.h"
#include "MotionStudio/model/GradientType.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/ShaderDefinition.h"
#include "MotionStudio/model/ShaderUniformValues.h"
#include "MotionStudio/model/StylePaintMode.h"
#include "MotionStudio/serialization/Serializer.h"
#include "MotionStudio/undo/AddGradientStopCommand.h"
#include "MotionStudio/undo/AddShaderCommand.h"
#include "MotionStudio/undo/RemoveGradientStopCommand.h"
#include "MotionStudio/undo/RemoveShaderCommand.h"
#include "MotionStudio/undo/SetGradientTypeCommand.h"
#include "MotionStudio/undo/SetStylePaintModeCommand.h"
#include "MotionStudio/undo/UpdateShaderDefinitionCommand.h"

#include "BridgeInternals.h"
#include "DocumentLock.h"
#include "MSDocument.h"

using namespace bridge;

using motion::AddGradientStopCommand;
using motion::AddShaderCommand;
using motion::Color;
using motion::Document;
using motion::EntityId;
using motion::FillStyle;
using motion::FindShader;
using motion::GradientType;
using motion::Layer;
using motion::LayerStyleType;
using motion::RemoveGradientStopCommand;
using motion::RemoveShaderCommand;
using motion::Serializer;
using motion::SetGradientTypeCommand;
using motion::SetStylePaintModeCommand;
using motion::ShaderDefinition;
using motion::ShaderIsReferenced;
using motion::ShaderUniformDecl;
using motion::StrokeStyle;
using motion::StylePaintMode;
using motion::UniformFormat;
using motion::UpdateShaderDefinitionCommand;
using motion::ValidateShaderReferences;

namespace {

constexpr const char *kDefaultMainImage = R"(vec4 mainImage(vec2 uv) {
    return vec4(uv, 0.0, 1.0);
})";

MS_UNIFORM_FORMAT ToMSUniformFormat(UniformFormat format) {
    switch (format) {
        case UniformFormat::Float:
            return MS_UNIFORM_FORMAT_FLOAT;
        case UniformFormat::Float2:
            return MS_UNIFORM_FORMAT_FLOAT2;
        case UniformFormat::Float3:
            return MS_UNIFORM_FORMAT_FLOAT3;
        case UniformFormat::Float4:
            return MS_UNIFORM_FORMAT_FLOAT4;
        case UniformFormat::Color:
            return MS_UNIFORM_FORMAT_COLOR;
        default:
            return MS_UNIFORM_FORMAT_INVALID;
    }
}

const ShaderDefinition *ConstFindShader(const Document &document, uint64_t shaderId) {
    return FindShader(document, EntityId{shaderId});
}

ShaderDefinition *MutableFindShader(Document &document, uint64_t shaderId) {
    return FindShader(document, EntityId{shaderId});
}

}  // namespace

int ms_document_shader_count(MSDocument *document) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return 0;
    }
    return static_cast<int>(document->document->shaders.size());
}

uint64_t ms_document_shader_id_at(MSDocument *document, int index) {
    DocumentLock lock(document);
    if (document == nullptr || index < 0 ||
        static_cast<size_t>(index) >= document->document->shaders.size()) {
        return 0;
    }
    return document->document->shaders[static_cast<size_t>(index)].id.value;
}

char *ms_document_shader_name(MSDocument *document, uint64_t shaderId) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return nullptr;
    }
    const ShaderDefinition *shader = ConstFindShader(*document->document, shaderId);
    if (shader == nullptr) {
        return nullptr;
    }
    return strdup(shader->name.c_str());
}

char *ms_document_shader_main_image(MSDocument *document, uint64_t shaderId) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return nullptr;
    }
    const ShaderDefinition *shader = ConstFindShader(*document->document, shaderId);
    if (shader == nullptr) {
        return nullptr;
    }
    return strdup(shader->mainImage.c_str());
}

int ms_document_shader_uniform_count(MSDocument *document, uint64_t shaderId) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return 0;
    }
    const ShaderDefinition *shader = ConstFindShader(*document->document, shaderId);
    if (shader == nullptr) {
        return 0;
    }
    return static_cast<int>(shader->uniforms.size());
}

char *ms_document_shader_uniform_name_at(MSDocument *document, uint64_t shaderId, int index) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return nullptr;
    }
    const ShaderDefinition *shader = ConstFindShader(*document->document, shaderId);
    if (shader == nullptr || index < 0 || static_cast<size_t>(index) >= shader->uniforms.size()) {
        return nullptr;
    }
    return strdup(shader->uniforms[static_cast<size_t>(index)].name.c_str());
}

MS_UNIFORM_FORMAT ms_document_shader_uniform_format_at(MSDocument *document, uint64_t shaderId,
                                                       int index) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return MS_UNIFORM_FORMAT_INVALID;
    }
    const ShaderDefinition *shader = ConstFindShader(*document->document, shaderId);
    if (shader == nullptr || index < 0 || static_cast<size_t>(index) >= shader->uniforms.size()) {
        return MS_UNIFORM_FORMAT_INVALID;
    }
    return ToMSUniformFormat(shader->uniforms[static_cast<size_t>(index)].format);
}

bool ms_document_shader_uniform_animatable_at(MSDocument *document, uint64_t shaderId, int index) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return false;
    }
    const ShaderDefinition *shader = ConstFindShader(*document->document, shaderId);
    if (shader == nullptr || index < 0 || static_cast<size_t>(index) >= shader->uniforms.size()) {
        return false;
    }
    return shader->uniforms[static_cast<size_t>(index)].animatable;
}

float ms_document_shader_uniform_default_float_at(MSDocument *document, uint64_t shaderId,
                                                  int index) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return 0.f;
    }
    const ShaderDefinition *shader = ConstFindShader(*document->document, shaderId);
    if (shader == nullptr || index < 0 || static_cast<size_t>(index) >= shader->uniforms.size()) {
        return 0.f;
    }
    return shader->uniforms[static_cast<size_t>(index)].defaultFloat;
}

void ms_document_shader_uniform_default_vec2_at(MSDocument *document, uint64_t shaderId, int index,
                                                float *x, float *y) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return;
    }
    const ShaderDefinition *shader = ConstFindShader(*document->document, shaderId);
    if (shader == nullptr || index < 0 || static_cast<size_t>(index) >= shader->uniforms.size()) {
        return;
    }
    const motion::Vec2 &value = shader->uniforms[static_cast<size_t>(index)].defaultFloat2;
    if (x != nullptr) {
        *x = value.x;
    }
    if (y != nullptr) {
        *y = value.y;
    }
}

void ms_document_shader_uniform_default_vec3_at(MSDocument *document, uint64_t shaderId, int index,
                                                float *x, float *y, float *z) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return;
    }
    const ShaderDefinition *shader = ConstFindShader(*document->document, shaderId);
    if (shader == nullptr || index < 0 || static_cast<size_t>(index) >= shader->uniforms.size()) {
        return;
    }
    const motion::Vec3 &value = shader->uniforms[static_cast<size_t>(index)].defaultFloat3;
    if (x != nullptr) {
        *x = value.x;
    }
    if (y != nullptr) {
        *y = value.y;
    }
    if (z != nullptr) {
        *z = value.z;
    }
}

void ms_document_shader_uniform_default_vec4_at(MSDocument *document, uint64_t shaderId, int index,
                                                float *x, float *y, float *z, float *w) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return;
    }
    const ShaderDefinition *shader = ConstFindShader(*document->document, shaderId);
    if (shader == nullptr || index < 0 || static_cast<size_t>(index) >= shader->uniforms.size()) {
        return;
    }
    const motion::Vec4 &value = shader->uniforms[static_cast<size_t>(index)].defaultFloat4;
    if (x != nullptr) {
        *x = value.x;
    }
    if (y != nullptr) {
        *y = value.y;
    }
    if (z != nullptr) {
        *z = value.z;
    }
    if (w != nullptr) {
        *w = value.w;
    }
}

void ms_document_shader_uniform_default_color_at(MSDocument *document, uint64_t shaderId, int index,
                                                 float *r, float *g, float *b, float *a) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return;
    }
    const ShaderDefinition *shader = ConstFindShader(*document->document, shaderId);
    if (shader == nullptr || index < 0 || static_cast<size_t>(index) >= shader->uniforms.size()) {
        return;
    }
    const motion::Color &value = shader->uniforms[static_cast<size_t>(index)].defaultColor;
    if (r != nullptr) {
        *r = value.r;
    }
    if (g != nullptr) {
        *g = value.g;
    }
    if (b != nullptr) {
        *b = value.b;
    }
    if (a != nullptr) {
        *a = value.a;
    }
}

uint64_t ms_document_add_shader(MSDocument *document, const char *name) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return 0;
    }
    ShaderDefinition shader;
    shader.name = (name != nullptr && name[0] != '\0') ? name : "Shader";
    shader.mainImage = kDefaultMainImage;
    const uint64_t id = shader.id.value;
    Execute(document, std::make_unique<AddShaderCommand>(std::move(shader)));
    return id;
}

bool ms_document_update_shader(MSDocument *document, uint64_t shaderId, const char *name,
                               const char *mainImage, const char *uniformsJson) {
    DocumentLock lock(document);
    if (document == nullptr || name == nullptr || mainImage == nullptr) {
        return false;
    }
    if (MutableFindShader(*document->document, shaderId) == nullptr) {
        return false;
    }
    const char *declsText = (uniformsJson == nullptr || uniformsJson[0] == '\0') ? "[]" : uniformsJson;
    auto decls = Serializer::deserializeUniformDecls(declsText);
    if (!decls.hasValue()) {
        return false;
    }
    Execute(document, std::make_unique<UpdateShaderDefinitionCommand>(EntityId{shaderId}, std::string(name), std::string(mainImage), std::move(decls).value()));
    return true;
}

bool ms_document_remove_shader(MSDocument *document, uint64_t shaderId) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return false;
    }
    const EntityId id{shaderId};
    if (FindShader(*document->document, id) == nullptr) {
        return false;
    }
    if (ShaderIsReferenced(*document->document, id)) {
        return false;
    }
    Execute(document, std::make_unique<RemoveShaderCommand>(id));
    return FindShader(*document->document, id) == nullptr;
}

bool ms_document_shader_is_referenced(MSDocument *document, uint64_t shaderId) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return false;
    }
    return ShaderIsReferenced(*document->document, EntityId{shaderId});
}

bool ms_document_rename_shader(MSDocument *document, uint64_t shaderId, const char *name) {
    DocumentLock lock(document);
    if (document == nullptr || name == nullptr) {
        return false;
    }
    ShaderDefinition *shader = MutableFindShader(*document->document, shaderId);
    if (shader == nullptr) {
        return false;
    }
    Execute(document, std::make_unique<UpdateShaderDefinitionCommand>(EntityId{shaderId}, std::string(name), shader->mainImage, shader->uniforms));
    return true;
}

char *ms_document_serialize_shaders(MSDocument *document) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return nullptr;
    }
    return strdup(Serializer::serializeShaders(*document->document).c_str());
}

MS_PAINT_MODE ms_layer_style_paint_mode_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock lock(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->styles.size()) {
        return MS_PAINT_MODE_INVALID;
    }
    motion::LayerStyle *style = layer->styles[static_cast<size_t>(index)].get();
    switch (style->type()) {
        case LayerStyleType::Fill:
            return static_cast<MS_PAINT_MODE>(static_cast<FillStyle *>(style)->paintMode);
        case LayerStyleType::Stroke:
            return static_cast<MS_PAINT_MODE>(static_cast<StrokeStyle *>(style)->paintMode);
    }
    return MS_PAINT_MODE_INVALID;
}

uint64_t ms_layer_style_shader_id_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock lock(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->styles.size()) {
        return 0;
    }
    motion::LayerStyle *style = layer->styles[static_cast<size_t>(index)].get();
    switch (style->type()) {
        case LayerStyleType::Fill:
            return static_cast<FillStyle *>(style)->shaderId.value;
        case LayerStyleType::Stroke:
            return static_cast<StrokeStyle *>(style)->shaderId.value;
    }
    return 0;
}

namespace {

StylePaintMode ToStylePaintMode(MS_PAINT_MODE mode) {
    switch (mode) {
        case MS_PAINT_MODE_COLOR:
            return StylePaintMode::Color;
        case MS_PAINT_MODE_SHADER:
            return StylePaintMode::Shader;
        case MS_PAINT_MODE_GRADIENT:
            return StylePaintMode::Gradient;
        case MS_PAINT_MODE_INVALID:
            break;
    }
    return StylePaintMode::Color;
}

GradientType ToGradientType(MS_GRADIENT_TYPE type) {
    switch (type) {
        case MS_GRADIENT_TYPE_LINEAR:
            return GradientType::Linear;
        case MS_GRADIENT_TYPE_RADIAL:
            return GradientType::Radial;
        case MS_GRADIENT_TYPE_CONIC:
            return GradientType::Conic;
        case MS_GRADIENT_TYPE_DIAMOND:
            return GradientType::Diamond;
        case MS_GRADIENT_TYPE_INVALID:
            break;
    }
    return GradientType::Linear;
}

MS_GRADIENT_TYPE FromGradientType(GradientType type) {
    return static_cast<MS_GRADIENT_TYPE>(type);
}

motion::GradientPaint *StyleGradient(motion::LayerStyle *style) {
    if (style->type() == LayerStyleType::Fill) {
        return &static_cast<FillStyle *>(style)->gradient;
    }
    if (style->type() == LayerStyleType::Stroke) {
        return &static_cast<StrokeStyle *>(style)->gradient;
    }
    return nullptr;
}

}  // namespace

bool ms_document_set_style_paint_mode(MSDocument *document, uint64_t layerId, int index,
                                      MS_PAINT_MODE mode, uint64_t shaderId) {
    DocumentLock lock(document);
    if (document == nullptr || mode == MS_PAINT_MODE_INVALID) {
        return false;
    }
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->styles.size()) {
        return false;
    }
    const StylePaintMode paintMode = ToStylePaintMode(mode);
    if (paintMode == StylePaintMode::Shader) {
        EntityId requested{shaderId};
        if (requested.isValid() && FindShader(*document->document, requested) == nullptr) {
            return false;
        }
        if (!requested.isValid() && document->document->shaders.empty()) {
            return false;
        }
    }
    Execute(document, std::make_unique<SetStylePaintModeCommand>(EntityId{layerId}, index, paintMode, EntityId{shaderId}));
    return true;
}

MS_GRADIENT_TYPE ms_layer_style_gradient_type_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock lock(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->styles.size()) {
        return MS_GRADIENT_TYPE_INVALID;
    }
    motion::GradientPaint *gradient = StyleGradient(layer->styles[static_cast<size_t>(index)].get());
    if (gradient == nullptr) {
        return MS_GRADIENT_TYPE_INVALID;
    }
    return FromGradientType(gradient->type);
}

int ms_layer_style_gradient_stop_count(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock lock(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->styles.size()) {
        return 0;
    }
    motion::GradientPaint *gradient = StyleGradient(layer->styles[static_cast<size_t>(index)].get());
    if (gradient == nullptr) {
        return 0;
    }
    return static_cast<int>(gradient->stops.size());
}

bool ms_document_set_gradient_type(MSDocument *document, uint64_t layerId, int index,
                                   MS_GRADIENT_TYPE type) {
    DocumentLock lock(document);
    if (document == nullptr || type == MS_GRADIENT_TYPE_INVALID) {
        return false;
    }
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->styles.size()) {
        return false;
    }
    Execute(document, std::make_unique<SetGradientTypeCommand>(EntityId{layerId}, index, ToGradientType(type)));
    return true;
}

bool ms_document_add_gradient_stop(MSDocument *document, uint64_t layerId, int index,
                                   int insertIndex, float r, float g, float b, float a,
                                   float position) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return false;
    }
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->styles.size()) {
        return false;
    }
    Execute(document, std::make_unique<AddGradientStopCommand>(EntityId{layerId}, index, insertIndex, Color{r, g, b, a}, position));
    return true;
}

bool ms_document_remove_gradient_stop(MSDocument *document, uint64_t layerId, int index,
                                      int stopIndex) {
    DocumentLock lock(document);
    if (document == nullptr) {
        return false;
    }
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 || static_cast<size_t>(index) >= layer->styles.size()) {
        return false;
    }
    Execute(document, std::make_unique<RemoveGradientStopCommand>(EntityId{layerId}, index, stopIndex));
    return true;
}
