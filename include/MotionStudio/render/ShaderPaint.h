#pragma once

#include <string>
#include <vector>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/common/Vec3.h"
#include "MotionStudio/model/ShaderDefinition.h"
#include "MotionStudio/model/ShaderUniformValues.h"

namespace motion {

// One evaluated user uniform for a shader paint snapshot (adapter setData).
struct EvaluatedShaderUniform {
    std::string name;
    ShaderUniformValueKind kind = ShaderUniformValueKind::AnimFloat;
    float floatValue = 0.f;
    Vec2 float2Value{};
    Vec3 float3Value{};
    Color colorValue{1, 1, 1, 1};
};

// Self-contained shader paint snapshot; adapter must not look up Document.
struct ShaderPaint {
    EntityId shaderId{};
    std::string mainImage;
    std::vector<ShaderUniformDecl> uniforms;
    std::vector<EvaluatedShaderUniform> values;
};

}  // namespace motion
