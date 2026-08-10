#include <string>

#include <gtest/gtest.h>

#include "motionstudio_bridge.h"

#if defined(__APPLE__)

TEST(PagExportBridgeTest, NullDocumentFails) {
    char *error = nullptr;
    MSPagExportOptions options{};
    options.outputPath = "/tmp/x.pag";
    options.allowBitmapExport = false;
    options.bitmapScale = 1.0f;
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
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    EXPECT_FALSE(ms_pag_export(document, compositionId, &options, &error));
    ASSERT_NE(error, nullptr);
    EXPECT_NE(std::string(error).find("path"), std::string::npos);
    ms_string_free(error);
    ms_document_destroy(document);
}

#endif
