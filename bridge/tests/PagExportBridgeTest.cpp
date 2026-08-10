#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "BridgeInternals.h"
#include "DocumentLock.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "motionstudio_bridge.h"

#if defined(__APPLE__)

TEST(PagExportBridgeTest, NullDocumentFails) {
    char *error = nullptr;
    MSPagExportOptions options{};
    options.outputPath = "/tmp/x.pag";
    options.allowBitmapExport = false;
    options.bitmapScale = 1.0f;
    options.bmpSequenceType = MS_PAG_BMP_SEQUENCE_AUTO;
    EXPECT_FALSE(ms_pag_export(nullptr, 0, &options, &error));
    ASSERT_NE(error, nullptr);
    EXPECT_NE(std::string(error).find("document"), std::string::npos);
    ms_string_free(error);
}

TEST(PagExportBridgeTest, EmptyPathFails) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    char *error = nullptr;
    MSPagExportOptions options{};
    options.outputPath = "";
    options.allowBitmapExport = false;
    options.bitmapScale = 1.0f;
    options.bmpSequenceType = MS_PAG_BMP_SEQUENCE_AUTO;
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    EXPECT_FALSE(ms_pag_export(document, compositionId, &options, &error));
    ASSERT_NE(error, nullptr);
    EXPECT_NE(std::string(error).find("path"), std::string::npos);
    ms_string_free(error);
    ms_document_destroy(document);
}

TEST(PagExportBridgeTest, CompositionBmpExportsWithRealFrameSource) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    ASSERT_NE(compositionId, 0u);

    {
        DocumentLock lock(document);
        motion::Composition *composition = bridge::FindComposition(document, compositionId);
        ASSERT_NE(composition, nullptr);
        composition->name = "Main_bmp";
        composition->width = 64;
        composition->height = 64;
        composition->duration = 1;
    }

    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);
    ASSERT_NE(layerId, 0u);

    const std::filesystem::path outputPath =
        std::filesystem::temp_directory_path() / "ms_pag_export_bmp_bridge_test.pag";
    std::error_code removeError;
    std::filesystem::remove(outputPath, removeError);

    MSPagExportOptions options{};
    const std::string pathString = outputPath.string();
    options.outputPath = pathString.c_str();
    options.allowBitmapExport = true;
    options.bitmapScale = 1.0f;
    options.bmpSequenceType = MS_PAG_BMP_SEQUENCE_BITMAP;

    char *error = nullptr;
    const bool ok = ms_pag_export(document, compositionId, &options, &error);
    EXPECT_TRUE(ok) << (error != nullptr ? error : "unknown error");
    if (error != nullptr) {
        ms_string_free(error);
    }

    ASSERT_TRUE(std::filesystem::exists(outputPath));
    const auto fileSize = std::filesystem::file_size(outputPath);
    EXPECT_GT(fileSize, 0u);

    std::filesystem::remove(outputPath, removeError);
    ms_document_destroy(document);
}

TEST(PagExportBridgeTest, CompositionBmpExportsVideoSequence) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    ASSERT_NE(compositionId, 0u);

    {
        DocumentLock lock(document);
        motion::Composition *composition = bridge::FindComposition(document, compositionId);
        ASSERT_NE(composition, nullptr);
        composition->name = "Main_bmp";
        composition->width = 64;
        composition->height = 64;
        composition->duration = 2;
    }

    ASSERT_NE(ms_command_add_rect_layer(document, compositionId), 0u);

    const std::filesystem::path outputPath =
        std::filesystem::temp_directory_path() / "ms_pag_export_video_bridge_test.pag";
    std::error_code removeError;
    std::filesystem::remove(outputPath, removeError);

    MSPagExportOptions options{};
    const std::string pathString = outputPath.string();
    options.outputPath = pathString.c_str();
    options.allowBitmapExport = true;
    options.bitmapScale = 1.0f;
    options.bmpSequenceType = MS_PAG_BMP_SEQUENCE_VIDEO;

    char *error = nullptr;
    const bool ok = ms_pag_export(document, compositionId, &options, &error);
    EXPECT_TRUE(ok) << (error != nullptr ? error : "unknown error");
    if (error != nullptr) {
        ms_string_free(error);
    }

    ASSERT_TRUE(std::filesystem::exists(outputPath));
    EXPECT_GT(std::filesystem::file_size(outputPath), 0u);

    std::filesystem::remove(outputPath, removeError);
    ms_document_destroy(document);
}

TEST(PagExportBridgeTest, BmpWithoutAllowBitmapExportFails) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);

    {
        DocumentLock lock(document);
        motion::Composition *composition = bridge::FindComposition(document, compositionId);
        ASSERT_NE(composition, nullptr);
        composition->name = "Main_bmp";
        composition->width = 64;
        composition->height = 64;
        composition->duration = 1;
    }
    ASSERT_NE(ms_command_add_rect_layer(document, compositionId), 0u);

    const std::filesystem::path outputPath =
        std::filesystem::temp_directory_path() / "ms_pag_export_bmp_denied.pag";
    std::error_code removeError;
    std::filesystem::remove(outputPath, removeError);

    MSPagExportOptions options{};
    const std::string pathString = outputPath.string();
    options.outputPath = pathString.c_str();
    options.allowBitmapExport = false;
    options.bitmapScale = 1.0f;
    options.bmpSequenceType = MS_PAG_BMP_SEQUENCE_AUTO;

    char *error = nullptr;
    EXPECT_FALSE(ms_pag_export(document, compositionId, &options, &error));
    ASSERT_NE(error, nullptr);
    ms_string_free(error);

    ms_document_destroy(document);
}

#endif
