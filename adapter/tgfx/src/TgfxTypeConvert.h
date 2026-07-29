#pragma once

#include <tgfx/core/BlendMode.h>
#include <tgfx/core/Color.h>
#include <tgfx/core/Matrix.h>
#include <tgfx/core/PathTypes.h>
#include <tgfx/core/Stroke.h>

#include "MotionStudio/common/Color.h"
#include "MotionStudio/common/Mat3.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/FillRule.h"
#include "MotionStudio/model/LineCap.h"
#include "MotionStudio/model/LineJoin.h"

namespace motion {

uint8_t ToByte(float value);

tgfx::Color ToTgfxColor(const Color &color);
tgfx::BlendMode ToTgfxBlendMode(BlendMode mode);
tgfx::LineCap ToTgfxLineCap(LineCap cap);
tgfx::LineJoin ToTgfxLineJoin(LineJoin join);
tgfx::PathFillType ToTgfxFillType(FillRule fillRule);
tgfx::Matrix ToTgfxMatrix(const Mat3 &matrix);

}  // namespace motion
