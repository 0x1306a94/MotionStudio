#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "motionstudio_bridge.h"

namespace {

struct BridgeString {
    char *value = nullptr;
    ~BridgeString() {
        ms_string_free(value);
    }
    std::string str() const {
        return value != nullptr ? value : "";
    }
};

}  // namespace

TEST(BridgeShaderTest, AddUpdateRemoveAndSerializeRoundTrip) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    EXPECT_EQ(ms_document_shader_count(document), 0);

    const uint64_t id = ms_document_add_shader(document, "Ripple");
    ASSERT_NE(id, 0u);
    EXPECT_EQ(ms_document_shader_count(document), 1);

    BridgeString name{ms_document_shader_name(document, id)};
    EXPECT_STREQ(name.value, "Ripple");

    const char *uniforms = R"([{"name":"rippleCount","format":"float","count":1}])";
    ASSERT_TRUE(ms_document_update_shader(
        document, id, "Ripple", "vec4 mainImage(vec2 uv) { return vec4(uv,0.0,1.0); }", uniforms));
    EXPECT_EQ(ms_document_shader_uniform_count(document, id), 1);
    EXPECT_EQ(ms_document_shader_uniform_format_at(document, id, 0), MS_UNIFORM_FORMAT_FLOAT);

    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);
    ASSERT_NE(layerId, 0u);
    ASSERT_GE(ms_layer_style_count(document, layerId), 1);
    ASSERT_TRUE(ms_document_set_style_paint_mode(document, layerId, 0, MS_PAINT_MODE_SHADER, id));
    EXPECT_EQ(ms_layer_style_paint_mode_at(document, layerId, 0), MS_PAINT_MODE_SHADER);
    EXPECT_EQ(ms_layer_style_shader_id_at(document, layerId, 0), id);
    EXPECT_FALSE(ms_document_remove_shader(document, id));

    BridgeString shadersJson{ms_document_serialize_shaders(document)};
    ASSERT_NE(shadersJson.value, nullptr);
    BridgeString documentJson{ms_document_save(document)};
    ASSERT_NE(documentJson.value, nullptr);

    ms_document_destroy(document);

    char *error = nullptr;
    MSDocument *loaded = ms_document_load_json_with_shaders(
        documentJson.value, std::strlen(documentJson.value), shadersJson.value,
        std::strlen(shadersJson.value), &error);
    ASSERT_NE(loaded, nullptr) << (error != nullptr ? error : "");
    EXPECT_EQ(error, nullptr);
    EXPECT_EQ(ms_document_shader_count(loaded), 1);
    EXPECT_EQ(ms_document_shader_id_at(loaded, 0), id);
    EXPECT_EQ(ms_layer_style_paint_mode_at(loaded, layerId, 0), MS_PAINT_MODE_SHADER);
    ms_document_destroy(loaded);
}

TEST(BridgeShaderTest, LoadWithoutShadersJsonYieldsEmptyLibrary) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    BridgeString documentJson{ms_document_save(document)};
    ASSERT_NE(documentJson.value, nullptr);
    ms_document_destroy(document);

    char *error = nullptr;
    MSDocument *loaded =
        ms_document_load_json_with_shaders(documentJson.value, std::strlen(documentJson.value),
                                           nullptr, 0, &error);
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(error, nullptr);
    EXPECT_EQ(ms_document_shader_count(loaded), 0);
    ms_document_destroy(loaded);
}

TEST(BridgeShaderTest, UniformColorAnimatableDefaultsAndVec4Property) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    const uint64_t shaderId = ms_document_add_shader(document, "Tint");
    ASSERT_NE(shaderId, 0u);

    const char *uniforms =
        R"([{"name":"tint","format":"color","count":1,"animatable":false,"default":[0.2,0.4,0.6,1]},)"
        R"({"name":"offset","format":"float4","count":1,"animatable":true,"default":[1,2,3,4]}])";
    ASSERT_TRUE(ms_document_update_shader(
        document, shaderId, "Tint", "vec4 mainImage(vec2 uv) { return tint; }", uniforms));

    EXPECT_EQ(ms_document_shader_uniform_format_at(document, shaderId, 0), MS_UNIFORM_FORMAT_COLOR);
    EXPECT_FALSE(ms_document_shader_uniform_animatable_at(document, shaderId, 0));
    float r = 0, g = 0, b = 0, a = 0;
    ms_document_shader_uniform_default_color_at(document, shaderId, 0, &r, &g, &b, &a);
    EXPECT_FLOAT_EQ(r, 0.2f);
    EXPECT_FLOAT_EQ(g, 0.4f);
    EXPECT_FLOAT_EQ(b, 0.6f);
    EXPECT_FLOAT_EQ(a, 1.f);

    EXPECT_EQ(ms_document_shader_uniform_format_at(document, shaderId, 1), MS_UNIFORM_FORMAT_FLOAT4);
    EXPECT_TRUE(ms_document_shader_uniform_animatable_at(document, shaderId, 1));
    float x = 0, y = 0, z = 0, w = 0;
    ms_document_shader_uniform_default_vec4_at(document, shaderId, 1, &x, &y, &z, &w);
    EXPECT_FLOAT_EQ(x, 1.f);
    EXPECT_FLOAT_EQ(y, 2.f);
    EXPECT_FLOAT_EQ(z, 3.f);
    EXPECT_FLOAT_EQ(w, 4.f);

    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);
    ASSERT_TRUE(ms_document_set_style_paint_mode(document, layerId, 0, MS_PAINT_MODE_SHADER, shaderId));
    const char *offsetPath = "styles[0].uniformValues.offset";
    EXPECT_EQ(ms_property_type(document, layerId, offsetPath), MS_VALUE_VEC4);
    ms_command_set_static_vec4(document, layerId, offsetPath, 5, 6, 7, 8);
    ms_property_evaluate_vec4(document, layerId, offsetPath, 0, &x, &y, &z, &w);
    EXPECT_FLOAT_EQ(x, 5.f);
    EXPECT_FLOAT_EQ(y, 6.f);
    EXPECT_FLOAT_EQ(z, 7.f);
    EXPECT_FLOAT_EQ(w, 8.f);
    ms_command_add_keyframe_vec4(document, layerId, offsetPath, 10, 9, 8, 7, 6);
    EXPECT_TRUE(ms_property_is_animated(document, layerId, offsetPath));

    ms_document_destroy(document);
}

TEST(BridgeShaderTest, RenameAndRemoveUnreferenced) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    const uint64_t id = ms_document_add_shader(document, "A");
    ASSERT_TRUE(ms_document_rename_shader(document, id, "B"));
    BridgeString name{ms_document_shader_name(document, id)};
    EXPECT_STREQ(name.value, "B");
    ASSERT_TRUE(ms_document_remove_shader(document, id));
    EXPECT_EQ(ms_document_shader_count(document), 0);
    ms_document_destroy(document);
}

TEST(BridgeShaderTest, GradientPaintModeAndStops) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);
    ASSERT_TRUE(ms_document_set_style_paint_mode(document, layerId, 0, MS_PAINT_MODE_GRADIENT, 0));
    EXPECT_EQ(ms_layer_style_paint_mode_at(document, layerId, 0), MS_PAINT_MODE_GRADIENT);
    EXPECT_EQ(ms_layer_style_gradient_type_at(document, layerId, 0), MS_GRADIENT_TYPE_LINEAR);
    EXPECT_EQ(ms_layer_style_gradient_stop_count(document, layerId, 0), 2);

    ASSERT_TRUE(ms_document_set_gradient_type(document, layerId, 0, MS_GRADIENT_TYPE_RADIAL));
    EXPECT_EQ(ms_layer_style_gradient_type_at(document, layerId, 0), MS_GRADIENT_TYPE_RADIAL);
    ASSERT_TRUE(ms_document_add_gradient_stop(document, layerId, 0, 1, 1, 0, 0, 1, 0.5f));
    EXPECT_EQ(ms_layer_style_gradient_stop_count(document, layerId, 0), 3);
    ASSERT_TRUE(ms_document_remove_gradient_stop(document, layerId, 0, 1));
    EXPECT_EQ(ms_layer_style_gradient_stop_count(document, layerId, 0), 2);

    // Default rect is 200×200; gradient endpoints are AABB-relative with 15% inset.
    float x = 0, y = 0;
    ms_property_evaluate_vec2(document, layerId, "styles[0].gradient.start", 0, &x, &y);
    EXPECT_FLOAT_EQ(x, 30.f);
    EXPECT_FLOAT_EQ(y, 100.f);
    ms_property_evaluate_vec2(document, layerId, "styles[0].gradient.end", 0, &x, &y);
    EXPECT_FLOAT_EQ(x, 170.f);
    EXPECT_FLOAT_EQ(y, 100.f);
    ms_document_destroy(document);
}
