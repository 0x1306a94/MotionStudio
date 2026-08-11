#pragma once

#include <string>

#include "MotionStudio/common/Expected.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/GradientPaint.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/ShaderDefinition.h"

namespace motion {

// Explicitly clears shader paint fields and switches the style to Color mode.
// Not used by kind switching (kind switch preserves shader fields).
void ClearShaderPaint(FillStyle &style);
void ClearShaderPaint(StrokeStyle &style);

// Binds a shader: sets Shader mode, shaderId, and default uniformValues.
// Fails when shader.id is invalid.
Expected<void, std::string> BindShaderPaint(FillStyle &style, const ShaderDefinition &shader);
Expected<void, std::string> BindShaderPaint(StrokeStyle &style, const ShaderDefinition &shader);

// True when stops.size() >= 2, first position == 0, last == 1, and intermediate
// positions are strictly increasing (evaluated as static values).
bool GradientStopsAreValid(const GradientPaint &gradient);

// Fills default Linear black→white gradient when stops.size() < 2.
// start/end: layer-local endpoints (typically AABB mid-left → mid-right).
void EnsureDefaultGradient(GradientPaint &gradient, Vec2 start, Vec2 end);

// Layer-local mid-left → mid-right of the shape AABB at `time`.
// Falls back to (0,0)→(100,0) when bounds are unavailable. Returns false on
// fallback.
bool DefaultGradientEndpoints(const Layer &layer, FrameTime time, Vec2 &outStart, Vec2 &outEnd);

}  // namespace motion
