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
