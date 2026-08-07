#pragma once

#include <string>
#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/UniformFormat.h"

namespace motion {

// One uniform entry in a shader scheme (name + GPU format + array count).
struct ShaderUniformDecl {
    std::string name;
    UniformFormat format = UniformFormat::Float;
    int count = 1;
};

// Document-owned process-color (color source) definition: source + scheme only.
// Runtime Fill/Stroke instances hold per-instance uniform values separately.
struct ShaderDefinition {
    EntityId id = EntityId::Generate();
    std::string name;
    // User mainImage(uv) body compiled by the adapter into a fragment program.
    std::string mainImage;
    std::vector<ShaderUniformDecl> uniforms;
};

}  // namespace motion
