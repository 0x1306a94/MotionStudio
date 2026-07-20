#pragma once

#include <string>

#include "MotionStudio/animation/Easing.h"
#include "MotionStudio/common/Expected.h"
#include "MotionStudio/model/AssetType.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/FillRule.h"
#include "MotionStudio/model/LayerType.h"
#include "MotionStudio/model/LineCap.h"
#include "MotionStudio/model/LineJoin.h"
#include "MotionStudio/model/MaskMode.h"
#include "MotionStudio/model/ShapeType.h"

// JSON v1 schema 的枚举字符串映射（camelCase，与 Lottie 生态对齐）。
// 文件结构见 Serializer.cpp；迁移只操作 JSON，不依赖运行时模型。
namespace motion::dto {

inline constexpr int SCHEMA_VERSION = 1;

// 枚举 → 字符串不失败（全覆盖 + 兜底）；字符串 → 枚举未知时返回 Error。
const char* ToString(LayerType type);
Expected<LayerType> layerTypeFromString(const std::string& text);

const char* ToString(ShapeType type);
Expected<ShapeType> shapeTypeFromString(const std::string& text);

const char* ToString(FillRule rule);
Expected<FillRule> fillRuleFromString(const std::string& text);

const char* ToString(LineCap cap);
Expected<LineCap> lineCapFromString(const std::string& text);

const char* ToString(LineJoin join);
Expected<LineJoin> lineJoinFromString(const std::string& text);

const char* ToString(BlendMode mode);
Expected<BlendMode> blendModeFromString(const std::string& text);

const char* ToString(MaskMode mode);
Expected<MaskMode> maskModeFromString(const std::string& text);

const char* ToString(AssetType type);
Expected<AssetType> assetTypeFromString(const std::string& text);

const char* ToString(Easing::Type type);
Expected<Easing::Type> easingTypeFromString(const std::string& text);

}  // namespace motion::dto
