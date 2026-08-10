#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "MotionStudio/animation/Animatable.h"
#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Expected.h"
#include "MotionStudio/common/UniformFormat.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/common/Vec3.h"
#include "MotionStudio/common/Vec4.h"
#include "MotionStudio/model/ShaderDefinition.h"

namespace motion {

class Document;

// Storage kind for one user-editable shader uniform value.
// v1: Float/Float2/Float3/Float4→AnimFloat4, Color→AnimColor; other kinds reserved.
enum class ShaderUniformValueKind : uint8_t {
    AnimFloat = 0,
    AnimFloat2 = 1,
    AnimFloat3 = 2,
    AnimColor = 3,
    StaticInt = 4,
    AnimFloat4 = 5,
    StaticMat3 = 6,
    TextureAsset = 7,
};

// One named uniform value aligned to a ShaderDefinition scheme entry.
struct ShaderUniformValue {
    std::string name;
    ShaderUniformValueKind kind = ShaderUniformValueKind::AnimFloat;
    Animatable<float> floatValue{0.f};
    Animatable<Vec2> float2Value{Vec2{0, 0}};
    Animatable<Vec3> float3Value{Vec3{0, 0, 0}};
    Animatable<Vec4> float4Value{Vec4{0, 0, 0, 0}};
    Animatable<Color> colorValue{Color{1, 1, 1, 1}};
};

// Ordered list of per-instance uniform values for a Fill/Stroke shader paint.
struct ShaderUniformValues {
    std::vector<ShaderUniformValue> entries;
};

// Maps a UniformFormat to the v1 animatable value kind.
// format: scheme format to map.
// Returns Unexpected for formats not supported as user uniforms in v1.
Expected<ShaderUniformValueKind, std::string> KindForFormat(UniformFormat format);

// Returns success when format maps to kind under v1 rules.
// format: scheme format.
// kind: value kind to check against format.
Expected<void, std::string> FormatSupportsAnimKind(UniformFormat format, ShaderUniformValueKind kind);

// Builds default uniform values for a shader scheme (caller must pass legal decls).
// decls: scheme uniforms in declaration order.
ShaderUniformValues MakeDefaultUniformValues(const std::vector<ShaderUniformDecl> &decls);

// Reorders/rebuilds values to match decls: keep same name+kind, drop extras, add defaults.
// decls: new scheme uniforms.
// previous: existing per-instance values before realignment.
Expected<ShaderUniformValues, std::string> RealignUniformValues(const std::vector<ShaderUniformDecl> &decls,
                                                                const ShaderUniformValues &previous);

// Finds a shader definition by id by scanning Document.shaders (not EntityIndex).
// document: document whose shader library is searched.
// id: shader entity id.
ShaderDefinition *FindShader(Document &document, EntityId id);
const ShaderDefinition *FindShader(const Document &document, EntityId id);

// Returns true if any layer style currently references shaderId.
// document: document to scan.
// shaderId: shader entity id to look up.
bool ShaderIsReferenced(const Document &document, EntityId shaderId);

}  // namespace motion
