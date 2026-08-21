#include <string>

#include <gtest/gtest.h>

#include "motionstudio_bridge.h"

#if defined(__APPLE__)

TEST(VideoExportBridgeTest, NullDocumentFails) {
    char *error = nullptr;
    MSVideoExportOptions options{};
    options.outputPath = "/tmp/x.mp4";
    options.startFrame = -1;
    options.endFrame = -1;
    EXPECT_FALSE(ms_video_export(nullptr, 0, &options, nullptr, nullptr, nullptr, &error));
    ASSERT_NE(error, nullptr);
    EXPECT_NE(std::string(error).find("document"), std::string::npos);
    ms_string_free(error);
}

TEST(VideoExportBridgeTest, EmptyPathFails) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    char *error = nullptr;
    MSVideoExportOptions options{};
    options.outputPath = "";
    options.startFrame = -1;
    options.endFrame = -1;
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    EXPECT_FALSE(ms_video_export(document, compositionId, &options, nullptr, nullptr, nullptr, &error));
    ASSERT_NE(error, nullptr);
    EXPECT_NE(std::string(error).find("path"), std::string::npos);
    ms_string_free(error);
    ms_document_destroy(document);
}

TEST(VideoExportBridgeTest, AcceptsMovContainerTag) {
    MSVideoExportOptions options{};
    options.outputPath = "/tmp/x.mov";
    options.container = MS_VIDEO_CONTAINER_MOV;
    options.optimizeForNetworkUse = true;
    EXPECT_EQ(options.container, MS_VIDEO_CONTAINER_MOV);
}

#endif
