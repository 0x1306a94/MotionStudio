#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/UniformFormat.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/model/LayerStyle.h"
#include "MotionStudio/model/LayerStylePaint.h"
#include "MotionStudio/model/LayerType.h"
#include "MotionStudio/model/ShaderDefinition.h"
#include "MotionStudio/model/ShaderUniformValues.h"
#include "MotionStudio/model/StylePaintMode.h"
#include "MotionStudio/undo/AddShaderCommand.h"
#include "MotionStudio/undo/RemoveShaderCommand.h"
#include "MotionStudio/undo/SetStylePaintModeCommand.h"
#include "MotionStudio/undo/UndoManager.h"
#include "MotionStudio/undo/UpdateShaderDefinitionCommand.h"

using motion::AddShaderCommand;
using motion::BindShaderPaint;
using motion::Composition;
using motion::Document;
using motion::EntityId;
using motion::FillStyle;
using motion::Layer;
using motion::LayerType;
using motion::RemoveShaderCommand;
using motion::SetStylePaintModeCommand;
using motion::ShaderDefinition;
using motion::ShaderIsReferenced;
using motion::ShaderUniformDecl;
using motion::ShaderUniformValueKind;
using motion::StylePaintMode;
using motion::UndoManager;
using motion::UniformFormat;
using motion::UpdateShaderDefinitionCommand;

namespace {

ShaderDefinition MakeRippleShader() {
    ShaderDefinition shader;
    shader.name = "Ripple";
    shader.mainImage = "vec4 mainImage(vec2 uv){ return vec4(uv,0.0,1.0); }";
    shader.uniforms.push_back(ShaderUniformDecl{"rippleCount", UniformFormat::Float, 1});
    shader.uniforms.push_back(ShaderUniformDecl{"tint", UniformFormat::Float4, 1});
    return shader;
}

struct ShaderScene {
    Document document;
    UndoManager undo;
    Composition *composition = nullptr;
    Layer *layer = nullptr;

    ShaderScene() {
        composition = document.addComposition(std::make_unique<Composition>());
        layer = document.addLayer(composition->id, std::make_unique<Layer>(LayerType::Shape));
    }

    template <typename CommandType, typename... Args>
    void execute(Args &&...args) {
        undo.execute(document, std::make_unique<CommandType>(std::forward<Args>(args)...));
    }
};

FillStyle *AddBoundFill(Layer *layer, const ShaderDefinition &shader) {
    auto fill = std::make_unique<FillStyle>();
    BindShaderPaint(*fill, shader);
    FillStyle *raw = fill.get();
    layer->styles.push_back(std::move(fill));
    return raw;
}

}  // namespace

TEST(ShaderCommandTest, RemoveShaderSkippedWhenReferenced) {
    ShaderScene scene;
    ShaderDefinition shader = MakeRippleShader();
    const EntityId shaderId = shader.id;
    scene.document.shaders.push_back(shader);
    AddBoundFill(scene.layer, scene.document.shaders[0]);
    ASSERT_TRUE(ShaderIsReferenced(scene.document, shaderId));

    scene.execute<RemoveShaderCommand>(shaderId);
    EXPECT_EQ(scene.document.shaders.size(), 1u);
    EXPECT_EQ(scene.document.shaders[0].id, shaderId);
}

TEST(ShaderCommandTest, RemoveShaderSucceedsWhenUnreferenced) {
    ShaderScene scene;
    ShaderDefinition shader = MakeRippleShader();
    const EntityId shaderId = shader.id;
    scene.document.shaders.push_back(shader);
    ASSERT_FALSE(ShaderIsReferenced(scene.document, shaderId));

    scene.execute<RemoveShaderCommand>(shaderId);
    EXPECT_TRUE(scene.document.shaders.empty());

    scene.undo.undo(scene.document);
    ASSERT_EQ(scene.document.shaders.size(), 1u);
    EXPECT_EQ(scene.document.shaders[0].id, shaderId);

    scene.undo.redo(scene.document);
    EXPECT_TRUE(scene.document.shaders.empty());
}

TEST(ShaderCommandTest, SetPaintModeToShaderBindsDefaults) {
    ShaderScene scene;
    ShaderDefinition shader = MakeRippleShader();
    const EntityId shaderId = shader.id;
    scene.document.shaders.push_back(shader);

    auto fill = std::make_unique<FillStyle>();
    scene.layer->styles.push_back(std::move(fill));
    auto *fillPtr = static_cast<FillStyle *>(scene.layer->styles[0].get());
    ASSERT_EQ(fillPtr->paintMode, StylePaintMode::Color);

    scene.execute<SetStylePaintModeCommand>(scene.layer->id, 0, StylePaintMode::Shader, shaderId);
    EXPECT_EQ(fillPtr->paintMode, StylePaintMode::Shader);
    EXPECT_EQ(fillPtr->shaderId, shaderId);
    ASSERT_FALSE(fillPtr->uniformValues.entries.empty());
    EXPECT_EQ(fillPtr->uniformValues.entries[0].name, "rippleCount");
    EXPECT_EQ(fillPtr->uniformValues.entries[0].kind, ShaderUniformValueKind::AnimFloat);

    scene.undo.undo(scene.document);
    EXPECT_EQ(fillPtr->paintMode, StylePaintMode::Color);
    EXPECT_FALSE(fillPtr->shaderId.isValid());
    EXPECT_TRUE(fillPtr->uniformValues.entries.empty());
}

TEST(ShaderCommandTest, UpdateShaderDefinitionRealignsReferencingStyles) {
    ShaderScene scene;
    ShaderDefinition shader = MakeRippleShader();
    const EntityId shaderId = shader.id;
    scene.document.shaders.push_back(shader);
    FillStyle *fill = AddBoundFill(scene.layer, scene.document.shaders[0]);
    ASSERT_EQ(fill->uniformValues.entries.size(), 2u);
    fill->uniformValues.entries[0].floatValue.setStaticValue(5.f);

    std::vector<ShaderUniformDecl> newUniforms = {
        ShaderUniformDecl{"rippleCount", UniformFormat::Float, 1},
        ShaderUniformDecl{"speed", UniformFormat::Float, 1},
    };
    scene.execute<UpdateShaderDefinitionCommand>(shaderId, std::string{"Ripple2"},
                                                 std::string{"vec4 mainImage(vec2 uv){ return vec4(1.0); }"},
                                                 newUniforms);

    ASSERT_EQ(scene.document.shaders.size(), 1u);
    EXPECT_EQ(scene.document.shaders[0].name, "Ripple2");
    ASSERT_EQ(scene.document.shaders[0].uniforms.size(), 2u);
    EXPECT_EQ(scene.document.shaders[0].uniforms[1].name, "speed");

    ASSERT_EQ(fill->uniformValues.entries.size(), 2u);
    EXPECT_FLOAT_EQ(fill->uniformValues.entries[0].floatValue.staticValue(), 5.f);
    EXPECT_EQ(fill->uniformValues.entries[1].name, "speed");

    scene.undo.undo(scene.document);
    EXPECT_EQ(scene.document.shaders[0].name, "Ripple");
    ASSERT_EQ(fill->uniformValues.entries.size(), 2u);
    EXPECT_EQ(fill->uniformValues.entries[1].name, "tint");
    EXPECT_FLOAT_EQ(fill->uniformValues.entries[0].floatValue.staticValue(), 5.f);
}

TEST(ShaderCommandTest, AddShaderUndoRedo) {
    ShaderScene scene;
    ShaderDefinition shader = MakeRippleShader();
    const EntityId shaderId = shader.id;

    scene.execute<AddShaderCommand>(shader);
    ASSERT_EQ(scene.document.shaders.size(), 1u);
    EXPECT_EQ(scene.document.shaders[0].id, shaderId);

    scene.undo.undo(scene.document);
    EXPECT_TRUE(scene.document.shaders.empty());

    scene.undo.redo(scene.document);
    ASSERT_EQ(scene.document.shaders.size(), 1u);
    EXPECT_EQ(scene.document.shaders[0].id, shaderId);
}
