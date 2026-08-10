#pragma once

#include <cstddef>
#include <cstdint>

namespace motion {

// Uniform variable formats for GPU programs and document shader schemes.
// Order and names must stay stable for serialization and adapter interop.
enum class UniformFormat {
    Float,                   // 32-bit floating point scalar.
    Float2,                  // 2-component vector of 32-bit floating point values.
    Float3,                  // 3-component vector of 32-bit floating point values.
    Float4,                  // 4-component vector of 32-bit floating point values.
    Float2x2,                // 2x2 matrix of 32-bit floating point values.
    Float3x3,                // 3x3 matrix of 32-bit floating point values.
    Float4x4,                // 4x4 matrix of 32-bit floating point values.
    Int,                     // 32-bit signed integer scalar.
    Int2,                    // 2-component vector of 32-bit signed integer values.
    Int3,                    // 3-component vector of 32-bit signed integer values.
    Int4,                    // 4-component vector of 32-bit signed integer values.
    Texture2DSampler,        // 2D texture sampler.
    TextureExternalSampler,  // External texture sampler (e.g. for camera input).
    Texture2DRectSampler,    // Rectangle texture sampler.
    Color,                   // RGBA color; GPU layout matches Float4 (vec4).
};

// Returns the GLSL type name for the given uniform format.
// format: uniform format to map.
// Returns an empty string for unknown formats.
const char *UniformFormatGLSLTypeName(UniformFormat format);

// Returns true if the format is a texture sampler (not part of a std140 UBO).
// format: uniform format to test.
bool IsSamplerFormat(UniformFormat format);

// Returns the byte size of one element of the given uniform format.
// format: uniform format whose element size is requested.
// Sampler formats are represented as int32_t.
size_t UniformFormatByteSize(UniformFormat format);

}  // namespace motion
