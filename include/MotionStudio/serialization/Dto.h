#pragma once

#include <string>

#include "MotionStudio/common/Expected.h"
#include "MotionStudio/model/AssetType.h"
#include "MotionStudio/model/BlendMode.h"
#include "MotionStudio/model/FillRule.h"
#include "MotionStudio/model/LayerType.h"
#include "MotionStudio/model/LineCap.h"
#include "MotionStudio/model/LineJoin.h"
#include "MotionStudio/model/MaskMode.h"
#include "MotionStudio/model/ShapeType.h"
#include "MotionStudio/model/StrokePosition.h"
#include "MotionStudio/model/TrackMatteType.h"

// Enum ↔ string mappings for the JSON v1 schema (camelCase, aligned with the
// Lottie ecosystem). File layout is defined in Serializer.cpp; migration
// operates purely on JSON and does not depend on runtime model types.
namespace motion::dto {

inline constexpr int SCHEMA_VERSION = 1;

// Enum → string always succeeds (full coverage + fallback).
// String → enum returns an error string on unknown values.
const char *ToString(LayerType type);
Expected<LayerType, std::string> layerTypeFromString(const std::string &text);

const char *ToString(ShapeType type);
Expected<ShapeType, std::string> shapeTypeFromString(const std::string &text);

const char *ToString(FillRule rule);
Expected<FillRule, std::string> fillRuleFromString(const std::string &text);

const char *ToString(LineCap cap);
Expected<LineCap, std::string> lineCapFromString(const std::string &text);

const char *ToString(LineJoin join);
Expected<LineJoin, std::string> lineJoinFromString(const std::string &text);

const char *ToString(StrokePosition position);
Expected<StrokePosition, std::string> strokePositionFromString(const std::string &text);

const char *ToString(BlendMode mode);
Expected<BlendMode, std::string> blendModeFromString(const std::string &text);

const char *ToString(MaskMode mode);
Expected<MaskMode, std::string> maskModeFromString(const std::string &text);

const char *ToString(TrackMatteType type);
Expected<TrackMatteType, std::string> trackMatteTypeFromString(const std::string &text);

const char *ToString(AssetType type);
Expected<AssetType, std::string> assetTypeFromString(const std::string &text);

}  // namespace motion::dto
