#pragma once

#include <string>

#include "MotionStudio/common/Expected.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/ShaderDefinition.h"

namespace motion {

// Clears shader paint fields and switches the style to Color mode.
// style: fill style to reset.
void ClearShaderPaint(FillStyle &style);

// Clears shader paint fields and switches the style to Color mode.
// style: stroke style to reset.
void ClearShaderPaint(StrokeStyle &style);

// Binds a shader paint: sets Shader mode, shaderId, and default uniformValues.
// style: fill style to bind.
// shader: document shader definition to reference.
// Returns success; fails when shader.id is invalid.
Expected<void, std::string> BindShaderPaint(FillStyle &style, const ShaderDefinition &shader);

// Binds a shader paint: sets Shader mode, shaderId, and default uniformValues.
// style: stroke style to bind.
// shader: document shader definition to reference.
// Returns success; fails when shader.id is invalid.
Expected<void, std::string> BindShaderPaint(StrokeStyle &style, const ShaderDefinition &shader);

}  // namespace motion
