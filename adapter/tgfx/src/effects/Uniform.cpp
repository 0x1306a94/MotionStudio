#include "Uniform.h"

namespace motion {

const char *UniformFormatGLSLTypeName(UniformFormat format) {
    switch (format) {
        case UniformFormat::Float:
            return "float";
        case UniformFormat::Float2:
            return "vec2";
        case UniformFormat::Float3:
            return "vec3";
        case UniformFormat::Float4:
            return "vec4";
        case UniformFormat::Float2x2:
            return "mat2";
        case UniformFormat::Float3x3:
            return "mat3";
        case UniformFormat::Float4x4:
            return "mat4";
        case UniformFormat::Int:
            return "int";
        case UniformFormat::Int2:
            return "ivec2";
        case UniformFormat::Int3:
            return "ivec3";
        case UniformFormat::Int4:
            return "ivec4";
        case UniformFormat::Texture2DSampler:
            return "sampler2D";
        case UniformFormat::TextureExternalSampler:
            return "samplerExternalOES";
        case UniformFormat::Texture2DRectSampler:
            return "sampler2DRect";
    }
    return "";
}

bool IsSamplerFormat(UniformFormat format) {
    switch (format) {
        case UniformFormat::Texture2DSampler:
        case UniformFormat::TextureExternalSampler:
        case UniformFormat::Texture2DRectSampler:
            return true;
        default:
            return false;
    }
}

size_t Uniform::size() const {
    switch (_format) {
        case UniformFormat::Float:
            return sizeof(float);
        case UniformFormat::Float2:
            return 2 * sizeof(float);
        case UniformFormat::Float3:
            return 3 * sizeof(float);
        case UniformFormat::Float4:  // fall-through
        case UniformFormat::Float2x2:
            return 4 * sizeof(float);
        case UniformFormat::Float3x3:
            return 9 * sizeof(float);
        case UniformFormat::Float4x4:
            return 16 * sizeof(float);
        case UniformFormat::Int:
            return sizeof(int32_t);
        case UniformFormat::Int2:
            return 2 * sizeof(int32_t);
        case UniformFormat::Int3:
            return 3 * sizeof(int32_t);
        case UniformFormat::Int4:
            return 4 * sizeof(int32_t);
        case UniformFormat::Texture2DSampler:
        case UniformFormat::TextureExternalSampler:
        case UniformFormat::Texture2DRectSampler:
            return sizeof(int32_t);  // Samplers are represented as integers.
    }
    return 0;
}

}  // namespace motion
