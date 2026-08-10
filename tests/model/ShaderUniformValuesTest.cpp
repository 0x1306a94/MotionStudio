#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "MotionStudio/animation/Keyframe.h"
#include "MotionStudio/common/UniformFormat.h"
#include "MotionStudio/common/Vec2.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ShaderDefinition.h"
#include "MotionStudio/model/ShaderUniformValues.h"

using motion::Animatable;
using motion::Document;
using motion::EntityId;
using motion::FindShader;
using motion::Keyframe;
using motion::KindForFormat;
using motion::MakeDefaultUniformValues;
using motion::RealignUniformValues;
using motion::ShaderDefinition;
using motion::ShaderIsReferenced;
using motion::ShaderUniformDecl;
using motion::ShaderUniformValue;
using motion::ShaderUniformValueKind;
using motion::ShaderUniformValues;
using motion::UniformFormat;
using motion::Vec2;

TEST(ShaderUniformValuesTest, MakeDefaultsForFloatAndColor) {
    std::vector<ShaderUniformDecl> decls = {
        {"rippleCount", UniformFormat::Float, 1},
        {"tint", UniformFormat::Color, 1},
    };
    auto values = MakeDefaultUniformValues(decls);
    ASSERT_EQ(values.entries.size(), 2u);
    EXPECT_EQ(values.entries[0].kind, ShaderUniformValueKind::AnimFloat);
    EXPECT_EQ(values.entries[0].name, "rippleCount");
    EXPECT_EQ(values.entries[1].kind, ShaderUniformValueKind::AnimColor);
    EXPECT_EQ(values.entries[1].name, "tint");
}

TEST(ShaderUniformValuesTest, Float4MapsToAnimFloat4) {
    auto kind = KindForFormat(UniformFormat::Float4);
    ASSERT_TRUE(kind.hasValue());
    EXPECT_EQ(*kind, ShaderUniformValueKind::AnimFloat4);
}

TEST(ShaderUniformValuesTest, DefaultFromDeclOnMake) {
    ShaderUniformDecl center{"center", UniformFormat::Float2, 1};
    center.animatable = false;
    center.defaultFloat2 = Vec2{50.f, 200.f};
    auto values = MakeDefaultUniformValues({center});
    ASSERT_EQ(values.entries.size(), 1u);
    EXPECT_FALSE(values.entries[0].float2Value.isAnimated());
    EXPECT_FLOAT_EQ(values.entries[0].float2Value.staticValue().x, 50.f);
    EXPECT_FLOAT_EQ(values.entries[0].float2Value.staticValue().y, 200.f);
}

TEST(ShaderUniformValuesTest, RealignFlattensKeyframesWhenNotAnimatable) {
    ShaderUniformValue prev;
    prev.name = "offset";
    prev.kind = ShaderUniformValueKind::AnimFloat;
    prev.floatValue.setStaticValue(0.f);
    prev.floatValue.addKeyframe(Keyframe<float>{0, 0.f});
    prev.floatValue.addKeyframe(Keyframe<float>{10, 1.f});
    ShaderUniformValues previous;
    previous.entries.push_back(prev);

    ShaderUniformDecl decl{"offset", UniformFormat::Float, 1};
    decl.animatable = false;
    auto realigned = RealignUniformValues({decl}, previous);
    ASSERT_TRUE(realigned.hasValue());
    EXPECT_FALSE(realigned->entries[0].floatValue.isAnimated());
    EXPECT_FLOAT_EQ(realigned->entries[0].floatValue.staticValue(), 0.f);
}

TEST(ShaderUniformValuesTest, ChangingDefaultDoesNotTouchExisting) {
    ShaderUniformValue prev;
    prev.name = "ringCount";
    prev.kind = ShaderUniformValueKind::AnimFloat;
    prev.floatValue.setStaticValue(20.f);
    ShaderUniformValues previous;
    previous.entries.push_back(prev);

    ShaderUniformDecl decl{"ringCount", UniformFormat::Float, 1};
    decl.defaultFloat = 99.f;
    auto realigned = RealignUniformValues({decl}, previous);
    ASSERT_TRUE(realigned.hasValue());
    EXPECT_FLOAT_EQ(realigned->entries[0].floatValue.staticValue(), 20.f);
}

TEST(ShaderUniformValuesTest, RealignDropsRemovedAndAddsNew) {
    ShaderUniformValues previous;
    ShaderUniformValue keep;
    keep.name = "rippleCount";
    keep.kind = ShaderUniformValueKind::AnimFloat;
    keep.floatValue = Animatable<float>{5.f};
    previous.entries.push_back(keep);

    std::vector<ShaderUniformDecl> decls = {
        {"rippleCount", UniformFormat::Float, 1},
        {"speed", UniformFormat::Float, 1},
    };
    auto realigned = RealignUniformValues(decls, previous);
    ASSERT_TRUE(realigned.hasValue());
    ASSERT_EQ(realigned->entries.size(), 2u);
    EXPECT_FLOAT_EQ(realigned->entries[0].floatValue.staticValue(), 5.f);
    EXPECT_EQ(realigned->entries[1].name, "speed");
    EXPECT_EQ(realigned->entries[1].kind, ShaderUniformValueKind::AnimFloat);
}

TEST(ShaderUniformValuesTest, RejectsIntFormatInV1) {
    auto kind = KindForFormat(UniformFormat::Int);
    EXPECT_FALSE(kind.hasValue());
}

TEST(ShaderUniformValuesTest, RejectsNonUnitCountOnRealign) {
    std::vector<ShaderUniformDecl> decls = {
        {"values", UniformFormat::Float, 4},
    };
    ShaderUniformValues previous;
    auto realigned = RealignUniformValues(decls, previous);
    EXPECT_FALSE(realigned.hasValue());
}

TEST(ShaderUniformValuesTest, FindShaderScansDocumentShaders) {
    Document document;
    ShaderDefinition shader;
    shader.name = "Ripple";
    const EntityId id = shader.id;
    document.shaders.push_back(shader);

    EXPECT_EQ(FindShader(document, id), &document.shaders[0]);
    EXPECT_EQ(FindShader(const_cast<const Document &>(document), id), &document.shaders[0]);
    EXPECT_EQ(FindShader(document, EntityId{}), nullptr);
}

TEST(ShaderUniformValuesTest, ShaderIsReferencedFalseWithoutPaintFields) {
    Document document;
    ShaderDefinition shader;
    document.shaders.push_back(shader);
    EXPECT_FALSE(ShaderIsReferenced(document, document.shaders[0].id));
}
