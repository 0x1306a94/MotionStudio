#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "MotionStudio/common/UniformFormat.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/ShaderDefinition.h"
#include "MotionStudio/model/ShaderUniformValues.h"

using motion::Animatable;
using motion::Document;
using motion::EntityId;
using motion::FindShader;
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
