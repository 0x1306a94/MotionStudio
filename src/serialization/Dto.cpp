#include "MotionStudio/serialization/Dto.h"

namespace motion::dto {

const char *ToString(LayerType type) {
    switch (type) {
        case LayerType::Shape: {
            return "shape";
        }
        case LayerType::Image: {
            return "image";
        }
        case LayerType::Text: {
            return "text";
        }
        case LayerType::Group: {
            return "group";
        }
        case LayerType::Precomp: {
            return "precomp";
        }
    }
    return "unknown";
}

Expected<LayerType, std::string> layerTypeFromString(const std::string &text) {
    if (text == "shape") {
        return LayerType::Shape;
    }
    if (text == "image") {
        return LayerType::Image;
    }
    if (text == "text") {
        return LayerType::Text;
    }
    if (text == "group") {
        return LayerType::Group;
    }
    if (text == "precomp") {
        return LayerType::Precomp;
    }
    return Unexpected(std::string("unknown layer type: " + text));
}

const char *ToString(ShapeType type) {
    switch (type) {
        case ShapeType::Path: {
            return "path";
        }
        case ShapeType::Rect: {
            return "rect";
        }
        case ShapeType::Ellipse: {
            return "ellipse";
        }
        case ShapeType::TrimPath: {
            return "trimPath";
        }
    }
    return "unknown";
}

Expected<ShapeType, std::string> shapeTypeFromString(const std::string &text) {
    if (text == "path") {
        return ShapeType::Path;
    }
    if (text == "rect") {
        return ShapeType::Rect;
    }
    if (text == "ellipse") {
        return ShapeType::Ellipse;
    }
    if (text == "trimPath") {
        return ShapeType::TrimPath;
    }
    return Unexpected(std::string("unknown shape type: " + text));
}

const char *ToString(FillRule rule) {
    return rule == FillRule::NonZero ? "nonZero" : "evenOdd";
}

Expected<FillRule, std::string> fillRuleFromString(const std::string &text) {
    if (text == "nonZero") {
        return FillRule::NonZero;
    }
    if (text == "evenOdd") {
        return FillRule::EvenOdd;
    }
    return Unexpected(std::string("unknown fill rule: " + text));
}

const char *ToString(LineCap cap) {
    switch (cap) {
        case LineCap::Butt: {
            return "butt";
        }
        case LineCap::Round: {
            return "round";
        }
        case LineCap::Square: {
            return "square";
        }
    }
    return "unknown";
}

Expected<LineCap, std::string> lineCapFromString(const std::string &text) {
    if (text == "butt") {
        return LineCap::Butt;
    }
    if (text == "round") {
        return LineCap::Round;
    }
    if (text == "square") {
        return LineCap::Square;
    }
    return Unexpected(std::string("unknown line cap: " + text));
}

const char *ToString(LineJoin join) {
    switch (join) {
        case LineJoin::Miter: {
            return "miter";
        }
        case LineJoin::Round: {
            return "round";
        }
        case LineJoin::Bevel: {
            return "bevel";
        }
    }
    return "unknown";
}

const char *ToString(StrokePosition position) {
    switch (position) {
        case StrokePosition::Center: {
            return "center";
        }
        case StrokePosition::Inside: {
            return "inside";
        }
        case StrokePosition::Outside: {
            return "outside";
        }
    }
    return "unknown";
}

Expected<StrokePosition, std::string> strokePositionFromString(const std::string &text) {
    if (text == "center") {
        return StrokePosition::Center;
    }
    if (text == "inside") {
        return StrokePosition::Inside;
    }
    if (text == "outside") {
        return StrokePosition::Outside;
    }
    return Unexpected(std::string("unknown stroke position: " + text));
}

Expected<LineJoin, std::string> lineJoinFromString(const std::string &text) {
    if (text == "miter") {
        return LineJoin::Miter;
    }
    if (text == "round") {
        return LineJoin::Round;
    }
    if (text == "bevel") {
        return LineJoin::Bevel;
    }
    return Unexpected(std::string("unknown line join: " + text));
}

const char *ToString(BlendMode mode) {
    switch (mode) {
        case BlendMode::Normal: {
            return "normal";
        }
        case BlendMode::Multiply: {
            return "multiply";
        }
        case BlendMode::Screen: {
            return "screen";
        }
        case BlendMode::Overlay: {
            return "overlay";
        }
        case BlendMode::Darken: {
            return "darken";
        }
        case BlendMode::Lighten: {
            return "lighten";
        }
        case BlendMode::ColorDodge: {
            return "color-dodge";
        }
        case BlendMode::ColorBurn: {
            return "color-burn";
        }
        case BlendMode::HardLight: {
            return "hard-light";
        }
        case BlendMode::SoftLight: {
            return "soft-light";
        }
        case BlendMode::Difference: {
            return "difference";
        }
        case BlendMode::Exclusion: {
            return "exclusion";
        }
        case BlendMode::Hue: {
            return "hue";
        }
        case BlendMode::Saturation: {
            return "saturation";
        }
        case BlendMode::Color: {
            return "color";
        }
        case BlendMode::Luminosity: {
            return "luminosity";
        }
        case BlendMode::Add: {
            return "add";
        }
    }
    return "unknown";
}

Expected<BlendMode, std::string> blendModeFromString(const std::string &text) {
    if (text == "normal") {
        return BlendMode::Normal;
    }
    if (text == "multiply") {
        return BlendMode::Multiply;
    }
    if (text == "screen") {
        return BlendMode::Screen;
    }
    if (text == "overlay") {
        return BlendMode::Overlay;
    }
    if (text == "darken") {
        return BlendMode::Darken;
    }
    if (text == "lighten") {
        return BlendMode::Lighten;
    }
    if (text == "color-dodge") {
        return BlendMode::ColorDodge;
    }
    if (text == "color-burn") {
        return BlendMode::ColorBurn;
    }
    if (text == "hard-light") {
        return BlendMode::HardLight;
    }
    if (text == "soft-light") {
        return BlendMode::SoftLight;
    }
    if (text == "difference") {
        return BlendMode::Difference;
    }
    if (text == "exclusion") {
        return BlendMode::Exclusion;
    }
    if (text == "hue") {
        return BlendMode::Hue;
    }
    if (text == "saturation") {
        return BlendMode::Saturation;
    }
    if (text == "color") {
        return BlendMode::Color;
    }
    if (text == "luminosity") {
        return BlendMode::Luminosity;
    }
    if (text == "add") {
        return BlendMode::Add;
    }
    return Unexpected(std::string("unknown blend mode: " + text));
}

const char *ToString(MaskMode mode) {
    switch (mode) {
        case MaskMode::Add: {
            return "add";
        }
        case MaskMode::Subtract: {
            return "subtract";
        }
        case MaskMode::Intersect: {
            return "intersect";
        }
    }
    return "unknown";
}

Expected<MaskMode, std::string> maskModeFromString(const std::string &text) {
    if (text == "add") {
        return MaskMode::Add;
    }
    if (text == "subtract") {
        return MaskMode::Subtract;
    }
    if (text == "intersect") {
        return MaskMode::Intersect;
    }
    return Unexpected(std::string("unknown mask mode: " + text));
}

const char *ToString(TrackMatteType type) {
    switch (type) {
        case TrackMatteType::None: {
            return "none";
        }
        case TrackMatteType::Alpha: {
            return "alpha";
        }
        case TrackMatteType::AlphaInverted: {
            return "alphaInverted";
        }
        case TrackMatteType::Luma: {
            return "luma";
        }
        case TrackMatteType::LumaInverted: {
            return "lumaInverted";
        }
    }
    return "unknown";
}

Expected<TrackMatteType, std::string> trackMatteTypeFromString(const std::string &text) {
    if (text == "none") {
        return TrackMatteType::None;
    }
    if (text == "alpha") {
        return TrackMatteType::Alpha;
    }
    if (text == "alphaInverted") {
        return TrackMatteType::AlphaInverted;
    }
    if (text == "luma") {
        return TrackMatteType::Luma;
    }
    if (text == "lumaInverted") {
        return TrackMatteType::LumaInverted;
    }
    return Unexpected(std::string("unknown track matte type: " + text));
}

const char *ToString(AssetType type) {
    (void)type;
    return "image";
}

Expected<AssetType, std::string> assetTypeFromString(const std::string &text) {
    if (text == "image") {
        return AssetType::Image;
    }
    return Unexpected(std::string("unknown asset type: " + text));
}

const char *ToString(ImageScaleMode mode) {
    switch (mode) {
        case ImageScaleMode::None: {
            return "none";
        }
        case ImageScaleMode::Stretch: {
            return "stretch";
        }
        case ImageScaleMode::LetterBox: {
            return "letterBox";
        }
        case ImageScaleMode::Zoom: {
            return "zoom";
        }
    }
    return "unknown";
}

Expected<ImageScaleMode, std::string> imageScaleModeFromString(const std::string &text) {
    if (text == "none") {
        return ImageScaleMode::None;
    }
    if (text == "stretch") {
        return ImageScaleMode::Stretch;
    }
    if (text == "letterBox") {
        return ImageScaleMode::LetterBox;
    }
    if (text == "zoom") {
        return ImageScaleMode::Zoom;
    }
    return Unexpected(std::string("unknown image scale mode: " + text));
}

const char *ToString(TextAlign align) {
    switch (align) {
        case TextAlign::Left: {
            return "left";
        }
        case TextAlign::Center: {
            return "center";
        }
        case TextAlign::Right: {
            return "right";
        }
    }
    return "unknown";
}

Expected<TextAlign, std::string> textAlignFromString(const std::string &text) {
    if (text == "left") {
        return TextAlign::Left;
    }
    if (text == "center") {
        return TextAlign::Center;
    }
    if (text == "right") {
        return TextAlign::Right;
    }
    return Unexpected(std::string("unknown text align: " + text));
}

const char *ToString(StylePaintMode mode) {
    switch (mode) {
        case StylePaintMode::Color: {
            return "color";
        }
        case StylePaintMode::Shader: {
            return "shader";
        }
    }
    return "unknown";
}

Expected<StylePaintMode, std::string> stylePaintModeFromString(const std::string &text) {
    if (text == "color") {
        return StylePaintMode::Color;
    }
    if (text == "shader") {
        return StylePaintMode::Shader;
    }
    return Unexpected(std::string("unknown style paint mode: " + text));
}

const char *ToString(UniformFormat format) {
    switch (format) {
        case UniformFormat::Float: {
            return "float";
        }
        case UniformFormat::Float2: {
            return "float2";
        }
        case UniformFormat::Float3: {
            return "float3";
        }
        case UniformFormat::Float4: {
            return "float4";
        }
        case UniformFormat::Float2x2: {
            return "float2x2";
        }
        case UniformFormat::Float3x3: {
            return "float3x3";
        }
        case UniformFormat::Float4x4: {
            return "float4x4";
        }
        case UniformFormat::Int: {
            return "int";
        }
        case UniformFormat::Int2: {
            return "int2";
        }
        case UniformFormat::Int3: {
            return "int3";
        }
        case UniformFormat::Int4: {
            return "int4";
        }
        case UniformFormat::Texture2DSampler: {
            return "texture2DSampler";
        }
        case UniformFormat::TextureExternalSampler: {
            return "textureExternalSampler";
        }
        case UniformFormat::Texture2DRectSampler: {
            return "texture2DRectSampler";
        }
    }
    return "unknown";
}

Expected<UniformFormat, std::string> uniformFormatFromString(const std::string &text) {
    if (text == "float") {
        return UniformFormat::Float;
    }
    if (text == "float2") {
        return UniformFormat::Float2;
    }
    if (text == "float3") {
        return UniformFormat::Float3;
    }
    if (text == "float4") {
        return UniformFormat::Float4;
    }
    if (text == "float2x2") {
        return UniformFormat::Float2x2;
    }
    if (text == "float3x3") {
        return UniformFormat::Float3x3;
    }
    if (text == "float4x4") {
        return UniformFormat::Float4x4;
    }
    if (text == "int") {
        return UniformFormat::Int;
    }
    if (text == "int2") {
        return UniformFormat::Int2;
    }
    if (text == "int3") {
        return UniformFormat::Int3;
    }
    if (text == "int4") {
        return UniformFormat::Int4;
    }
    if (text == "texture2DSampler") {
        return UniformFormat::Texture2DSampler;
    }
    if (text == "textureExternalSampler") {
        return UniformFormat::TextureExternalSampler;
    }
    if (text == "texture2DRectSampler") {
        return UniformFormat::Texture2DRectSampler;
    }
    return Unexpected(std::string("unknown uniform format: " + text));
}

const char *ToString(ShaderUniformValueKind kind) {
    switch (kind) {
        case ShaderUniformValueKind::AnimFloat: {
            return "animFloat";
        }
        case ShaderUniformValueKind::AnimFloat2: {
            return "animFloat2";
        }
        case ShaderUniformValueKind::AnimFloat3: {
            return "animFloat3";
        }
        case ShaderUniformValueKind::AnimColor: {
            return "animColor";
        }
        case ShaderUniformValueKind::StaticInt: {
            return "staticInt";
        }
        case ShaderUniformValueKind::AnimFloat4: {
            return "animFloat4";
        }
        case ShaderUniformValueKind::StaticMat3: {
            return "staticMat3";
        }
        case ShaderUniformValueKind::TextureAsset: {
            return "textureAsset";
        }
    }
    return "unknown";
}

Expected<ShaderUniformValueKind, std::string> shaderUniformValueKindFromString(
    const std::string &text) {
    if (text == "animFloat") {
        return ShaderUniformValueKind::AnimFloat;
    }
    if (text == "animFloat2") {
        return ShaderUniformValueKind::AnimFloat2;
    }
    if (text == "animFloat3") {
        return ShaderUniformValueKind::AnimFloat3;
    }
    if (text == "animColor") {
        return ShaderUniformValueKind::AnimColor;
    }
    if (text == "staticInt") {
        return ShaderUniformValueKind::StaticInt;
    }
    if (text == "animFloat4") {
        return ShaderUniformValueKind::AnimFloat4;
    }
    if (text == "staticMat3") {
        return ShaderUniformValueKind::StaticMat3;
    }
    if (text == "textureAsset") {
        return ShaderUniformValueKind::TextureAsset;
    }
    return Unexpected(std::string("unknown shader uniform value kind: " + text));
}

}  // namespace motion::dto
