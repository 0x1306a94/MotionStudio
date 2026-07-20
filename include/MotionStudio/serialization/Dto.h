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

// Enum ↔ string mappings for the JSON v1 schema (camelCase, aligned with the
// Lottie ecosystem). File layout is defined in Serializer.cpp; migration
// operates purely on JSON and does not depend on runtime model types.
namespace motion::dto {

inline constexpr int SCHEMA_VERSION = 1;

// Enum → string always succeeds (full coverage + fallback).
// String → enum returns Error on unknown values.
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
