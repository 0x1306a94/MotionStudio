#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "MotionStudio/export/VideoEncoder.h"
#include "MotionStudio/export/VideoExporter.h"
#include "MotionStudio/export/VideoFrameSource.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"

using motion::Composition;
using motion::Document;
using motion::EntityId;
using motion::Expected;
using motion::FrameTime;
using motion::H264Profile;
using motion::VideoContainer;
using motion::VideoEncoder;
using motion::VideoExporter;
using motion::VideoExportOptions;
using motion::VideoExportProgress;
using motion::VideoFrame;
using motion::VideoFrameSource;
using motion::VideoFrameStorage;

namespace {

struct FakeSource : VideoFrameSource {
    Expected<void, std::string> prepare(const Document &, EntityId,
                                        const VideoExportOptions &options) override {
        preparedWidth = options.width;
        preparedHeight = options.height;
        return Expected<void, std::string>();
    }
    Expected<VideoFrame, std::string> renderFrame(FrameTime time) override {
        times.push_back(time);
        VideoFrame frame;
        frame.width = preparedWidth;
        frame.height = preparedHeight;
        frame.storage = VideoFrameStorage::CpuRgba;
        static const uint8_t kPixel[4] = {255, 0, 0, 255};
        frame.rgba = kPixel;
        frame.rowBytes = 4;
        return frame;
    }
    void finish() override {
        finished = true;
    }

    int preparedWidth = 0;
    int preparedHeight = 0;
    std::vector<FrameTime> times;
    bool finished = false;
};

struct FakeEncoder : VideoEncoder {
    Expected<void, std::string> begin(const VideoExportOptions &options) override {
        begun = options;
        return Expected<void, std::string>();
    }
    Expected<void, std::string> appendFrame(const VideoFrame &,
                                            FrameTime presentationIndex) override {
        indices.push_back(presentationIndex);
        return Expected<void, std::string>();
    }
    Expected<void, std::string> end() override {
        ended = true;
        return Expected<void, std::string>();
    }
    void abort() override {
        aborted = true;
    }

    VideoExportOptions begun;
    std::vector<FrameTime> indices;
    bool ended = false;
    bool aborted = false;
};

Document MakeDoc(int width, int height, FrameTime duration) {
    Document document;
    auto composition = std::make_unique<Composition>();
    composition->width = width;
    composition->height = height;
    composition->duration = duration;
    composition->frameRate = {30, 1};
    composition->cornerRadius = 40.0f;
    document.addComposition(std::move(composition));
    return document;
}

}  // namespace

TEST(VideoExporterTest, ExportsDefaultRangeWithMonotonicPts) {
    Document document = MakeDoc(1920, 1080, 5);
    const EntityId compId = document.compositions[0]->id;
    FakeSource source;
    FakeEncoder encoder;
    VideoExportOptions options;
    options.outputPath = "/tmp/motionstudio_export_test.mp4";

    const auto result = VideoExporter::Export(document, compId, options, source, encoder);
    ASSERT_TRUE(result.hasValue()) << result.error();
    EXPECT_EQ(source.times, (std::vector<FrameTime>{0, 1, 2, 3, 4}));
    EXPECT_EQ(encoder.indices, (std::vector<FrameTime>{0, 1, 2, 3, 4}));
    EXPECT_TRUE(encoder.ended);
    EXPECT_FALSE(encoder.aborted);
    EXPECT_TRUE(source.finished);
    EXPECT_EQ(encoder.begun.width, 1920);
    EXPECT_EQ(encoder.begun.height, 1080);
    EXPECT_EQ(encoder.begun.bitrateBps, 6220800);
    EXPECT_EQ(encoder.begun.keyframeInterval, 60);
}

TEST(VideoExporterTest, CustomRangeAndProgressCancel) {
    Document document = MakeDoc(64, 64, 10);
    const EntityId compId = document.compositions[0]->id;
    FakeSource source;
    FakeEncoder encoder;
    VideoExportOptions options;
    options.outputPath = "/tmp/x.mp4";
    options.range = {2, 6};
    options.bitrateBps = 2000000;
    options.keyframeInterval = 10;

    int callbacks = 0;
    const auto result = VideoExporter::Export(
        document, compId, options, source, encoder, [&](VideoExportProgress progress) {
            ++callbacks;
            EXPECT_EQ(progress.totalFrames, 4);
            return progress.completedFrames < 2;
        });
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error(), "cancelled");
    EXPECT_TRUE(encoder.aborted);
    EXPECT_FALSE(encoder.ended);
    EXPECT_TRUE(source.finished);
    EXPECT_EQ(source.times.size(), 2u);
    EXPECT_EQ(encoder.indices, (std::vector<FrameTime>{0, 1}));
    EXPECT_GE(callbacks, 2);
}

TEST(VideoExporterTest, RejectsOddDimensions) {
    Document document = MakeDoc(1921, 1080, 1);
    FakeSource source;
    FakeEncoder encoder;
    VideoExportOptions options;
    options.outputPath = "/tmp/x.mp4";
    const auto result =
        VideoExporter::Export(document, document.compositions[0]->id, options, source, encoder);
    ASSERT_FALSE(result.hasValue());
    EXPECT_NE(result.error().find("even"), std::string::npos);
    EXPECT_FALSE(encoder.ended);
}

TEST(VideoExporterTest, RejectsEmptyOutputPath) {
    Document document = MakeDoc(64, 64, 1);
    FakeSource source;
    FakeEncoder encoder;
    VideoExportOptions options;
    const auto result =
        VideoExporter::Export(document, document.compositions[0]->id, options, source, encoder);
    ASSERT_FALSE(result.hasValue());
    EXPECT_NE(result.error().find("path"), std::string::npos);
}

TEST(VideoExporterTest, PassesContainerAndNetworkOptimize) {
    Document document = MakeDoc(64, 64, 1);
    const EntityId compId = document.compositions[0]->id;
    FakeSource source;
    FakeEncoder encoder;
    VideoExportOptions options;
    options.outputPath = "/tmp/motionstudio_export_test.mov";
    options.container = VideoContainer::Mov;
    options.optimizeForNetworkUse = true;

    const auto result = VideoExporter::Export(document, compId, options, source, encoder);
    ASSERT_TRUE(result.hasValue()) << result.error();
    EXPECT_EQ(encoder.begun.container, VideoContainer::Mov);
    EXPECT_TRUE(encoder.begun.optimizeForNetworkUse);
}

TEST(VideoExporterTest, AttachAudioDefaultFails) {
    FakeEncoder encoder;
    const auto result = encoder.attachAudio();
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error(), "audio not implemented");
}
