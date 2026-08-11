#pragma once

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/GradientType.h"
#include "MotionStudio/render/DrawCommand.h"
#include "MotionStudio/render/SceneState.h"

namespace motion {

enum class GradientHandleKind {
    None = 0,
    Start,
    End,
    StartAngle,
    EndAngle,
};

struct GradientEditTarget {
    EntityId layerId;
    int styleIndex = -1;
};

struct GradientEditHandles {
    bool valid = false;
    GradientEditTarget target;
    GradientType type = GradientType::Linear;
    Vec2 worldStart{};
    Vec2 worldEnd{};
    float startAngle = 0.f;
    float endAngle = 360.f;
    Mat3 worldTransform = Mat3::Identity();
};

// Resolves a Gradient paint on the evaluated layer. Returns false when the
// layer/style is missing or not a Gradient paint.
bool BuildGradientEditHandles(const SceneState &state, GradientEditTarget target,
                              GradientEditHandles &out);

// Picks the topmost gradient chrome handle under scenePoint.
GradientHandleKind HitTestGradientEdit(const GradientEditHandles &handles, Vec2 scenePoint,
                                       float hitRadius);

// Draws gradient geometry chrome (line / circle / diamond / angle rays) and
// handle markers. strokeWidth / handleSize are in scene units.
DrawCommandList BuildGradientEditCommands(const GradientEditHandles &handles, float strokeWidth,
                                          float handleSize);

}  // namespace motion
