#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "motionstudio_bridge.h"

#if defined(__APPLE__)

TEST(SvgImportBridgeTest, NullDocumentFails) {
    char *error = nullptr;
    MSSvgImportOptions options{};
    options.insertIndex = -1;
    options.parentLayerId = 0;
    options.rootName = nullptr;
    MSSvgImportResult out{};
    const char svg[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\"/>";
    EXPECT_FALSE(ms_document_import_svg(nullptr, 0, svg, sizeof(svg) - 1, &options, &out, nullptr,
                                        &error));
    ASSERT_NE(error, nullptr);
    EXPECT_NE(std::string(error).find("document"), std::string::npos);
    ms_string_free(error);
}

TEST(SvgImportBridgeTest, InvalidXmlFails) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    char *error = nullptr;
    MSSvgImportOptions options{};
    options.insertIndex = -1;
    options.parentLayerId = 0;
    options.rootName = "SVG";
    MSSvgImportResult out{};
    const char svg[] = "not-svg";
    EXPECT_FALSE(ms_document_import_svg(document, compositionId, svg, sizeof(svg) - 1, &options,
                                        &out, nullptr, &error));
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(ms_composition_layer_count(document, compositionId), 0);
    ms_string_free(error);
    ms_document_destroy(document);
}

TEST(SvgImportBridgeTest, ImportsRectAndUndo) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    MSSvgImportOptions options{};
    options.insertIndex = -1;
    options.parentLayerId = 0;
    options.rootName = "SVG";
    MSSvgImportResult out{};
    char *diagnostics = nullptr;
    char *error = nullptr;
    const char svg[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"20\" height=\"10\">"
        "<rect x=\"0\" y=\"0\" width=\"20\" height=\"10\" fill=\"#f00\"/>"
        "</svg>";
    ASSERT_TRUE(ms_document_import_svg(document, compositionId, svg, sizeof(svg) - 1, &options, &out,
                                       &diagnostics, &error))
        << (error != nullptr ? error : "unknown");
    EXPECT_EQ(error, nullptr);
    EXPECT_NE(out.rootLayerId, 0u);
    EXPECT_EQ(out.sourceWidth, 20);
    EXPECT_EQ(out.sourceHeight, 10);
    EXPECT_EQ(ms_layer_type(document, out.rootLayerId), MS_LAYER_GROUP);
    EXPECT_GE(ms_composition_layer_count(document, compositionId), 1);
    if (diagnostics != nullptr) {
        ms_string_free(diagnostics);
    }
    ASSERT_TRUE(ms_document_undo(document));
    EXPECT_EQ(ms_composition_layer_count(document, compositionId), 0);
    ms_document_destroy(document);
}

TEST(SvgImportBridgeTest, WritesDataUriImageUnderProjectRoot) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const auto root = std::filesystem::temp_directory_path() /
        ("ms_svg_bridge_" + std::to_string(reinterpret_cast<uintptr_t>(document)));
    std::filesystem::create_directories(root / "assets");
    ms_document_set_project_root(document, root.string().c_str());

    const char svg[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\">"
        "<image href=\"data:image/png;base64,"
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==\""
        " width=\"16\" height=\"16\"/>"
        "</svg>";
    MSSvgImportOptions options{};
    options.insertIndex = -1;
    options.parentLayerId = 0;
    options.rootName = "SVG";
    MSSvgImportResult out{};
    char *error = nullptr;
    ASSERT_TRUE(ms_document_import_svg(document, compositionId, svg, sizeof(svg) - 1, &options, &out,
                                       nullptr, &error))
        << (error != nullptr ? error : "unknown");
    EXPECT_EQ(ms_document_asset_count(document), 1);
    const uint64_t assetId = ms_document_asset_id_at(document, 0);
    char *path = ms_asset_path(document, assetId);
    ASSERT_NE(path, nullptr);
    EXPECT_TRUE(std::filesystem::exists(root / path));
    ms_string_free(path);
    std::error_code removeError;
    std::filesystem::remove_all(root, removeError);
    ms_document_destroy(document);
}

TEST(SvgImportBridgeTest, ExternalImageReturnsDiagnostics) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const char svg[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\">"
        "<image href=\"photo.png\" width=\"16\" height=\"16\"/>"
        "</svg>";
    MSSvgImportOptions options{};
    options.insertIndex = -1;
    options.parentLayerId = 0;
    options.rootName = nullptr;
    MSSvgImportResult out{};
    char *diagnostics = nullptr;
    char *error = nullptr;
    ASSERT_TRUE(ms_document_import_svg(document, compositionId, svg, sizeof(svg) - 1, &options, &out,
                                       &diagnostics, &error));
    ASSERT_NE(diagnostics, nullptr);
    EXPECT_NE(std::string(diagnostics).find("image.external"), std::string::npos);
    ms_string_free(diagnostics);
    ms_document_destroy(document);
}

#endif
