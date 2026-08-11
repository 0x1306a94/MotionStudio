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
// start/end: AABB top-left-space endpoints (typically inset mid-left → mid-right).
void EnsureDefaultGradient(GradientPaint &gradient, Vec2 start, Vec2 end);

// Shape-path-space AABB of the layer content at `time`. Returns false when
// unavailable (outMin/outMax unchanged).
bool LayerShapeLocalAABB(const Layer &layer, FrameTime time, Vec2 &outMin, Vec2 &outMax);

// Default start/end in AABB top-left space: horizontal mid-line inset 15% from
// each side ((0.15w, h/2) → (0.85w, h/2)). Falls back to (0,0)→(100,0).
// Returns false on fallback.
bool DefaultGradientEndpoints(const Layer &layer, FrameTime time, Vec2 &outStart, Vec2 &outEnd);

// Returns a copy of `source` with every sample translated by `offset`.
Animatable<Vec2> OffsetAnimatableVec2(const Animatable<Vec2> &source, Vec2 offset);

}  // namespace motion
