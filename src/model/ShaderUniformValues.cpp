#include "MotionStudio/model/ShaderUniformValues.h"

#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/StylePaintMode.h"

namespace motion {

namespace {

const char *UnsupportedUniformFormatMessage() {
    return "unsupported uniform format for v1";
}

const char *UnsupportedUniformCountMessage() {
    return "unsupported uniform count for v1";
}

Expected<ShaderUniformValueKind, std::string> KindForDecl(const ShaderUniformDecl &decl) {
    if (decl.count != 1) {
        return Unexpected(std::string(UnsupportedUniformCountMessage()));
    }
    return KindForFormat(decl.format);
}

ShaderUniformValue MakeDefaultUniformValue(const std::string &name, ShaderUniformValueKind kind) {
    ShaderUniformValue value;
    value.name = name;
    value.kind = kind;
    return value;
}

const ShaderUniformValue *FindPreviousEntry(const ShaderUniformValues &previous, const std::string &name) {
    for (const ShaderUniformValue &entry : previous.entries) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace

Expected<ShaderUniformValueKind, std::string> KindForFormat(UniformFormat format) {
    switch (format) {
        case UniformFormat::Float: {
            return ShaderUniformValueKind::AnimFloat;
        }
        case UniformFormat::Float2: {
            return ShaderUniformValueKind::AnimFloat2;
        }
        case UniformFormat::Float3: {
            return ShaderUniformValueKind::AnimFloat3;
        }
        case UniformFormat::Float4: {
            return ShaderUniformValueKind::AnimFloat4;
        }
        case UniformFormat::Color: {
            return ShaderUniformValueKind::AnimColor;
        }
        default: {
            return Unexpected(std::string(UnsupportedUniformFormatMessage()));
        }
    }
}

Expected<void, std::string> FormatSupportsAnimKind(UniformFormat format, ShaderUniformValueKind kind) {
    Expected<ShaderUniformValueKind, std::string> expected = KindForFormat(format);
    if (!expected.hasValue()) {
        return Unexpected(expected.error());
    }
    if (*expected != kind) {
        return Unexpected(std::string("uniform format does not match value kind"));
    }
    return {};
}

ShaderUniformValues MakeDefaultUniformValues(const std::vector<ShaderUniformDecl> &decls) {
    ShaderUniformValues values;
    values.entries.reserve(decls.size());
    for (const ShaderUniformDecl &decl : decls) {
        Expected<ShaderUniformValueKind, std::string> kind = KindForDecl(decl);
        if (!kind.hasValue()) {
            continue;
        }
        values.entries.push_back(MakeDefaultUniformValue(decl.name, *kind));
    }
    return values;
}

Expected<ShaderUniformValues, std::string> RealignUniformValues(const std::vector<ShaderUniformDecl> &decls,
                                                                const ShaderUniformValues &previous) {
    ShaderUniformValues values;
    values.entries.reserve(decls.size());
    for (const ShaderUniformDecl &decl : decls) {
        Expected<ShaderUniformValueKind, std::string> kind = KindForDecl(decl);
        if (!kind.hasValue()) {
            return Unexpected(kind.error());
        }
        const ShaderUniformValue *previousEntry = FindPreviousEntry(previous, decl.name);
        if (previousEntry != nullptr && previousEntry->kind == *kind) {
            values.entries.push_back(*previousEntry);
            continue;
        }
        values.entries.push_back(MakeDefaultUniformValue(decl.name, *kind));
    }
    return values;
}

ShaderDefinition *FindShader(Document &document, EntityId id) {
    if (!id.isValid()) {
        return nullptr;
    }
    for (ShaderDefinition &shader : document.shaders) {
        if (shader.id == id) {
            return &shader;
        }
    }
    return nullptr;
}

const ShaderDefinition *FindShader(const Document &document, EntityId id) {
    if (!id.isValid()) {
        return nullptr;
    }
    for (const ShaderDefinition &shader : document.shaders) {
        if (shader.id == id) {
            return &shader;
        }
    }
    return nullptr;
}

bool ShaderIsReferenced(const Document &document, EntityId shaderId) {
    if (!shaderId.isValid()) {
        return false;
    }
    for (const auto &composition : document.compositions) {
        if (composition == nullptr) {
            continue;
        }
        for (const auto &layer : composition->layers) {
            if (layer == nullptr) {
                continue;
            }
            for (const auto &style : layer->styles) {
                if (style == nullptr) {
                    continue;
                }
                if (style->type() == LayerStyleType::Fill) {
                    const auto &fill = static_cast<const FillStyle &>(*style);
                    if (fill.paintMode == StylePaintMode::Shader && fill.shaderId == shaderId) {
                        return true;
                    }
                } else if (style->type() == LayerStyleType::Stroke) {
                    const auto &stroke = static_cast<const StrokeStyle &>(*style);
                    if (stroke.paintMode == StylePaintMode::Shader && stroke.shaderId == shaderId) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

}  // namespace motion
