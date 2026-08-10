#pragma once

#include <string>
#include <vector>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/UniformFormat.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/common/Vec3.h"
#include "MotionStudio/common/Vec4.h"

namespace motion {

// One uniform entry in a shader scheme (name + GPU format + array count +
// authoring metadata). Optional JSON fields: animatable, default.
struct ShaderUniformDecl {
    std::string name;
    UniformFormat format = UniformFormat::Float;
    int count = 1;
    bool animatable = true;
    float defaultFloat = 0.f;
    Vec2 defaultFloat2{0, 0};
    Vec3 defaultFloat3{0, 0, 0};
    Vec4 defaultFloat4{0, 0, 0, 0};
    Color defaultColor{1, 1, 1, 1};
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
