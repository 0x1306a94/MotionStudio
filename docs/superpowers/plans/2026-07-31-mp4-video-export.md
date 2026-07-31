# MP4 Video Export (H.264) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 库层可切换的 H.264 MP4 导出：Core `VideoExporter` 编排 + Apple 零拷贝 `TgfxVideoFrameSource` + `AvfVideoEncoder` + bridge API（无 UI）。

**Architecture:** Core `export/` 定义 `VideoFrame` / `VideoEncoder` / `VideoFrameSource` / `VideoExporter`；Apple 在 `adapter/avf`（AVAssetWriter）与 `adapter/tgfx`（CVPixelBuffer 渲染，强制 `cornerRadius=0`）注入实现；bridge 组装默认路径。后续 FFmpeg 只替换 `VideoEncoder`。

**Tech Stack:** C++17 core、GoogleTest、tgfx Metal、AVFoundation / CoreVideo、extern C bridge。

**Spec:** `docs/superpowers/specs/2026-07-31-mp4-video-export-design.md`

## Global Constraints

- 库层 only：无导出 UI。
- Core 不链接 AVFoundation / FFmpeg / tgfx；encoder 与 frame source 实现不进 `motionstudio_core`。
- 首版无声 MP4；`attachAudio()` 默认返回 `"audio not implemented"`，Exporter 不调用它。
- 导出忽略 composition `cornerRadius`（`beginFrame(..., 0)`）；背景按不透明写满帧。
- 宽高须为正偶数；奇数不自动 pad。
- 同步 `Export` + progress 回调（返回 false → `"cancelled"`）；调用期间 Document 只读。
- 零拷贝优先：`VideoFrameStorage::PlatformShared`（Apple = `CVPixelBufferRef`）；可降级 `CpuRgba`。
- Commit 信息：120 字符内英语、句号结尾（见 `.claude/rules/git-workflow.md`）。
- Expected 断言用 `hasValue()` / `error()`，不用 `EXPECT_THROW`。

## File Map

| 区域 | 文件 |
|---|---|
| Core 接口 | `include/MotionStudio/export/{VideoExportOptions,VideoFrame,VideoEncoder,VideoFrameSource,VideoExporter}.h` |
| Core 实现 | `src/export/VideoExporter.cpp`（含 resolve） |
| Core 测试 | `tests/export/VideoExporterTest.cpp` |
| CMake | `src/CMakeLists.txt`、`tests/CMakeLists.txt`、根 `CMakeLists.txt` |
| AVF 编码 | `adapter/avf/`（新建静态库 `motionstudio_avf_adapter`） |
| 零拷贝帧源 | `adapter/tgfx/include/TgfxVideoFrameSource.h`、`adapter/tgfx/src/TgfxVideoFrameSource.mm` |
| Bridge | `bridge/include/motionstudio_bridge.h`、`bridge/src/apple/motionstudio_bridge_video_export.mm`、`bridge/CMakeLists.txt` |
| 文档 | `docs/rendering.md`（导出小节） |

---

### Task 1: Core export 头文件 + CMake 接入

**Files:**
- Create: `include/MotionStudio/export/VideoExportOptions.h`
- Create: `include/MotionStudio/export/VideoFrame.h`
- Create: `include/MotionStudio/export/VideoEncoder.h`
- Create: `include/MotionStudio/export/VideoFrameSource.h`
- Create: `include/MotionStudio/export/VideoExporter.h`
- Create: `src/export/VideoExporter.cpp`（先提供可链接的 stub：`Export` 返回 `"not implemented"`）
- Modify: `src/CMakeLists.txt`（加入 `export` 源）
- Modify: `tests/CMakeLists.txt`（加入 `tests/export` 目录扫描）

**Interfaces:**
- Produces: 与 spec §2 一致的类型与 `VideoExporter::Export` 签名（见下）

- [x] **Step 1: 添加头文件**

`VideoExportOptions.h`:

```cpp
#pragma once

#include <string>

#include "MotionStudio/common/Time.h"

namespace motion {

enum class H264Profile { Baseline, Main, High };

struct VideoExportOptions {
    std::string outputPath;
    TimeRange range;
    int width = 0;
    int height = 0;
    FrameRate frameRate;
    int bitrateBps = 0;
    int keyframeInterval = 0;
    H264Profile profile = H264Profile::High;
};

struct VideoExportProgress {
    FrameTime completedFrames = 0;
    FrameTime totalFrames = 0;
};

}  // namespace motion
```

`VideoFrame.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace motion {

enum class VideoFrameStorage { CpuRgba, PlatformShared };

struct VideoFrame {
    int width = 0;
    int height = 0;
    VideoFrameStorage storage = VideoFrameStorage::CpuRgba;
    const uint8_t *rgba = nullptr;
    size_t rowBytes = 0;
    bool premultiplied = true;
    void *platformHandle = nullptr;
    void (*retainHandle)(void *) = nullptr;
    void (*releaseHandle)(void *) = nullptr;
};

}  // namespace motion
```

`VideoEncoder.h`:

```cpp
#pragma once

#include <string>

#include "MotionStudio/common/Expected.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/export/VideoExportOptions.h"
#include "MotionStudio/export/VideoFrame.h"

namespace motion {

class VideoEncoder {
  public:
    virtual ~VideoEncoder() = default;
    virtual Expected<void, std::string> begin(const VideoExportOptions &options) = 0;
    virtual Expected<void, std::string> appendFrame(const VideoFrame &frame,
                                                    FrameTime presentationIndex) = 0;
    virtual Expected<void, std::string> attachAudio() {
        return Unexpected<std::string>("audio not implemented");
    }
    virtual Expected<void, std::string> end() = 0;
    virtual void abort() = 0;
};

}  // namespace motion
```

`VideoFrameSource.h`:

```cpp
#pragma once

#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Expected.h"
#include "MotionStudio/common/Time.h"
#include "MotionStudio/export/VideoExportOptions.h"
#include "MotionStudio/export/VideoFrame.h"

namespace motion {

class Document;

class VideoFrameSource {
  public:
    virtual ~VideoFrameSource() = default;
    virtual Expected<void, std::string> prepare(const Document &document, EntityId compositionId,
                                                const VideoExportOptions &options) = 0;
    virtual Expected<VideoFrame, std::string> renderFrame(FrameTime time) = 0;
    virtual void finish() = 0;
};

}  // namespace motion
```

`VideoExporter.h`:

```cpp
#pragma once

#include <functional>
#include <string>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Expected.h"
#include "MotionStudio/export/VideoEncoder.h"
#include "MotionStudio/export/VideoExportOptions.h"
#include "MotionStudio/export/VideoFrameSource.h"

namespace motion {

class Document;

class VideoExporter {
  public:
    static Expected<void, std::string> Export(
        const Document &document, EntityId compositionId, const VideoExportOptions &options,
        VideoFrameSource &frames, VideoEncoder &encoder,
        const std::function<bool(VideoExportProgress)> &onProgress = {});
};

}  // namespace motion
```

- [x] **Step 2: stub 实现 + CMake**

`src/export/VideoExporter.cpp`:

```cpp
#include "MotionStudio/export/VideoExporter.h"

namespace motion {

Expected<void, std::string> VideoExporter::Export(
    const Document &, EntityId, const VideoExportOptions &, VideoFrameSource &, VideoEncoder &,
    const std::function<bool(VideoExportProgress)> &) {
    return Unexpected<std::string>("not implemented");
}

}  // namespace motion
```

在 `src/CMakeLists.txt` 的 `SERIALIZATION_SOURCES` 旁增加：

```cmake
add_files_by_extension(EXPORT_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/export
)
```

并在 `list(APPEND CORE_SOURCES ...)` 中加入 `${EXPORT_SOURCES}`。

在 `tests/CMakeLists.txt` 增加：

```cmake
add_files_by_extension(EXPORT_TESTS_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/export
)
```

并 append 到 `TESTS_SOURCES`。

- [x] **Step 3: 编译确认**

```bash
cmake -B build -G Ninja -DMOTIONSTUDIO_ENABLE_ASAN=ON
cmake --build build --target core
```

Expected: 成功。

- [x] **Step 4: Commit**

```bash
git commit --only \
  include/MotionStudio/export/VideoExportOptions.h \
  include/MotionStudio/export/VideoFrame.h \
  include/MotionStudio/export/VideoEncoder.h \
  include/MotionStudio/export/VideoFrameSource.h \
  include/MotionStudio/export/VideoExporter.h \
  src/export/VideoExporter.cpp \
  src/CMakeLists.txt \
  tests/CMakeLists.txt \
  -m "Add core video export interfaces and CMake wiring."
```

---

### Task 2: VideoExporter 编排 + Fake 测试（TDD）

**Files:**
- Modify: `src/export/VideoExporter.cpp`（完整 resolve + 循环）
- Create: `tests/export/VideoExporterTest.cpp`

**Interfaces:**
- Consumes: Task 1 头文件；`Document::entityIndex().findComposition`；`Composition::{duration,width,height,frameRate}`；`TimeRange`；`FrameRate::toSeconds`
- Produces: 可用的 `VideoExporter::Export`（行为见 spec 伪代码）

- [x] **Step 1: 写失败测试**

`tests/export/VideoExporterTest.cpp`（完整文件）：

```cpp
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
using motion::Unexpected;
using motion::VideoEncoder;
using motion::VideoExportOptions;
using motion::VideoExportProgress;
using motion::VideoExporter;
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
    void finish() override { finished = true; }

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
    void abort() override { aborted = true; }

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
    EXPECT_EQ(encoder.begun.bitrateBps, 6220800);  // clamp(1920*1080*30*0.1, 1e6, 5e7)
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
    options.bitrateBps = 2'000'000;
    options.keyframeInterval = 10;

    int callbacks = 0;
    const auto result = VideoExporter::Export(
        document, compId, options, source, encoder,
        [&](VideoExportProgress progress) {
            ++callbacks;
            EXPECT_EQ(progress.totalFrames, 4);
            return progress.completedFrames < 2;  // cancel before third append
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

TEST(VideoExporterTest, AttachAudioDefaultFails) {
    FakeEncoder encoder;
    const auto result = encoder.attachAudio();
    ASSERT_FALSE(result.hasValue());
    EXPECT_EQ(result.error(), "audio not implemented");
}
```

注意：默认码率公式 `round(width * height * fps * 0.1)` 再 clamp；1920×1080×30×0.1 = 6220800。实现必须与此一致，否则改测试与实现到同一公式。

- [x] **Step 2: 跑测试确认失败**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='VideoExporterTest.*'
```

Expected: `ExportsDefaultRange...` 失败（`"not implemented"`）或链接后断言失败。

- [x] **Step 3: 实现 `VideoExporter::Export`**

在 `VideoExporter.cpp` 中实现 resolve + 循环。关键逻辑：

```cpp
#include "MotionStudio/export/VideoExporter.h"

#include <algorithm>
#include <cmath>

#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"

namespace motion {
namespace {

bool IsPositiveEven(int value) {
    return value > 0 && (value % 2) == 0;
}

Expected<VideoExportOptions, std::string> Resolve(const Document &document, EntityId compositionId,
                                                  const VideoExportOptions &options) {
    const Composition *composition = document.entityIndex().findComposition(compositionId);
    if (composition == nullptr) {
        return Unexpected<std::string>("composition not found");
    }
    if (options.outputPath.empty()) {
        return Unexpected<std::string>("output path is empty");
    }

    VideoExportOptions resolved = options;
    if (resolved.width == 0) {
        resolved.width = composition->width;
    }
    if (resolved.height == 0) {
        resolved.height = composition->height;
    }
    if (!IsPositiveEven(resolved.width) || !IsPositiveEven(resolved.height)) {
        return Unexpected<std::string>("export size must be positive even dimensions");
    }

    if (resolved.frameRate.den == 0) {
        resolved.frameRate = composition->frameRate;
    }
    if (resolved.frameRate.num == 0 || resolved.frameRate.den == 0) {
        return Unexpected<std::string>("invalid frame rate");
    }

    if (resolved.range.end <= resolved.range.start) {
        resolved.range = {0, composition->duration};
    }
    if (resolved.range.start < 0 || resolved.range.end > composition->duration ||
        resolved.range.end <= resolved.range.start) {
        return Unexpected<std::string>("invalid export range");
    }

    const double fps = static_cast<double>(resolved.frameRate.num) /
                       static_cast<double>(resolved.frameRate.den);
    if (resolved.bitrateBps <= 0) {
        const double raw =
            static_cast<double>(resolved.width) * static_cast<double>(resolved.height) * fps * 0.1;
        resolved.bitrateBps =
            static_cast<int>(std::clamp(raw, 1'000'000.0, 50'000'000.0));
    }
    if (resolved.keyframeInterval <= 0) {
        resolved.keyframeInterval = std::max(1, static_cast<int>(std::lround(fps * 2.0)));
    }
    return resolved;
}

}  // namespace

Expected<void, std::string> VideoExporter::Export(
    const Document &document, EntityId compositionId, const VideoExportOptions &options,
    VideoFrameSource &frames, VideoEncoder &encoder,
    const std::function<bool(VideoExportProgress)> &onProgress) {
    auto resolved = Resolve(document, compositionId, options);
    if (!resolved.hasValue()) {
        return Unexpected<std::string>(resolved.error());
    }

    auto prepared = frames.prepare(document, compositionId, *resolved);
    if (!prepared.hasValue()) {
        return Unexpected<std::string>(prepared.error());
    }

    auto begun = encoder.begin(*resolved);
    if (!begun.hasValue()) {
        frames.finish();
        return Unexpected<std::string>(begun.error());
    }

    const FrameTime start = resolved->range.start;
    const FrameTime end = resolved->range.end;
    const FrameTime total = end - start;
    for (FrameTime time = start; time < end; ++time) {
        VideoExportProgress progress{time - start, total};
        if (onProgress && !onProgress(progress)) {
            encoder.abort();
            frames.finish();
            return Unexpected<std::string>("cancelled");
        }
        auto frame = frames.renderFrame(time);
        if (!frame.hasValue()) {
            encoder.abort();
            frames.finish();
            return Unexpected<std::string>(frame.error());
        }
        auto appended = encoder.appendFrame(*frame, time - start);
        if (frame->releaseHandle != nullptr && frame->platformHandle != nullptr) {
            frame->releaseHandle(frame->platformHandle);
        }
        if (!appended.hasValue()) {
            encoder.abort();
            frames.finish();
            return Unexpected<std::string>(appended.error());
        }
    }

    auto ended = encoder.end();
    frames.finish();
    if (!ended.hasValue()) {
        encoder.abort();
        return Unexpected<std::string>(ended.error());
    }
    return Expected<void, std::string>();
}

}  // namespace motion
```

取消语义：progress 在 **append 之前**调用，`completedFrames` 为已完成数（即将渲染的是下一帧）。与测试 `completedFrames < 2` 时已 render/append 2 帧一致。

- [x] **Step 4: 跑测试确认通过**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='VideoExporterTest.*'
```

Expected: PASS。

- [x] **Step 5: Commit**

```bash
git commit --only src/export/VideoExporter.cpp tests/export/VideoExporterTest.cpp \
  -m "Implement video export orchestration with cancel and validation."
```

---

### Task 3: `adapter/avf` — AvfVideoEncoder

**Files:**
- Create: `adapter/avf/CMakeLists.txt`
- Create: `adapter/avf/include/AvfVideoEncoder.h`
- Create: `adapter/avf/src/AvfVideoEncoder.mm`
- Create: `adapter/avf/tests/AvfVideoEncoderTest.mm`（或 `.cpp` + ObjC++）
- Modify: 根 `CMakeLists.txt`：`if(APPLE) add_subdirectory(adapter/avf)`

**Interfaces:**
- Consumes: `VideoEncoder`、`VideoExportOptions`、`VideoFrame`
- Produces: `motion::AvfVideoEncoder`（`static std::unique_ptr<AvfVideoEncoder> Make()` 或默认构造）

- [ ] **Step 1: CMake 库骨架**

`adapter/avf/CMakeLists.txt`：静态库 `avf_adapter` → `OUTPUT_NAME motionstudio_avf_adapter`，`PUBLIC` 链 `core`，link frameworks：`AVFoundation`、`CoreMedia`、`CoreVideo`、`Foundation`。测试可执行文件 `avf_adapter_test` + `gtest_discover_tests`。

根 `CMakeLists.txt` 在 `adapter/tgfx` 旁：

```cmake
if (APPLE)
    add_subdirectory(adapter/tgfx)
    add_subdirectory(adapter/avf)
endif()
```

- [ ] **Step 2: 写失败测试（CPU 实心帧写出 MP4）**

```objc
// adapter/avf/tests/AvfVideoEncoderTest.mm
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>
#import <AVFoundation/AVFoundation.h>

#include "AvfVideoEncoder.h"
#include "MotionStudio/export/VideoExportOptions.h"

using motion::AvfVideoEncoder;
using motion::H264Profile;
using motion::VideoExportOptions;
using motion::VideoFrame;
using motion::VideoFrameStorage;

TEST(AvfVideoEncoderTest, WritesMp4FromCpuFrames) {
    const auto path =
        (std::filesystem::temp_directory_path() / "motionstudio_avf_smoke.mp4").string();
    std::filesystem::remove(path);

    AvfVideoEncoder encoder;
    VideoExportOptions options;
    options.outputPath = path;
    options.width = 64;
    options.height = 64;
    options.frameRate = {30, 1};
    options.bitrateBps = 1'000'000;
    options.keyframeInterval = 30;
    options.profile = H264Profile::High;
    options.range = {0, 3};

    ASSERT_TRUE(encoder.begin(options).hasValue());
    std::vector<uint8_t> rgba(64 * 64 * 4, 0);
    for (size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = 255;
        rgba[i + 3] = 255;
    }
    VideoFrame frame;
    frame.width = 64;
    frame.height = 64;
    frame.storage = VideoFrameStorage::CpuRgba;
    frame.rgba = rgba.data();
    frame.rowBytes = 64 * 4;
    frame.premultiplied = true;
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(encoder.appendFrame(frame, i).hasValue()) << i;
    }
    ASSERT_TRUE(encoder.end().hasValue());
    ASSERT_TRUE(std::filesystem::exists(path));
    ASSERT_GT(std::filesystem::file_size(path), 0u);

    NSURL *url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    AVURLAsset *asset = [AVURLAsset URLAssetWithURL:url options:nil];
    EXPECT_GT(asset.duration.timescale, 0);
    std::filesystem::remove(path);
}

TEST(AvfVideoEncoderTest, AbortRemovesPartialFile) {
    const auto path =
        (std::filesystem::temp_directory_path() / "motionstudio_avf_abort.mp4").string();
    std::filesystem::remove(path);
    AvfVideoEncoder encoder;
    VideoExportOptions options;
    options.outputPath = path;
    options.width = 64;
    options.height = 64;
    options.frameRate = {30, 1};
    options.bitrateBps = 1'000'000;
    options.keyframeInterval = 30;
    ASSERT_TRUE(encoder.begin(options).hasValue());
    encoder.abort();
    EXPECT_FALSE(std::filesystem::exists(path));
}
```

- [ ] **Step 3: 跑测试确认失败/无法链接**

```bash
cmake -B build -G Ninja -DMOTIONSTUDIO_ENABLE_ASAN=ON
cmake --build build --target avf_adapter_test
```

Expected: 找不到 `AvfVideoEncoder` 或测试 FAIL。

- [ ] **Step 4: 实现 `AvfVideoEncoder`**

头文件要点：

```cpp
#pragma once
#include <memory>
#include <string>
#include "MotionStudio/export/VideoEncoder.h"

namespace motion {
class AvfVideoEncoder : public VideoEncoder {
  public:
    AvfVideoEncoder();
    ~AvfVideoEncoder() override;
    Expected<void, std::string> begin(const VideoExportOptions &options) override;
    Expected<void, std::string> appendFrame(const VideoFrame &frame,
                                            FrameTime presentationIndex) override;
    Expected<void, std::string> end() override;
    void abort() override;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
```

`AvfVideoEncoder.mm` 实现要点：

1. `begin`：删已存在 `outputPath`；创建 `AVAssetWriter`（`AVFileTypeMPEG4`）；`AVAssetWriterInput` + `AVAssetWriterInputPixelBufferAdaptor`；`AVVideoCodecTypeH264`；设置 `AVVideoAverageBitRateKey`、`AVVideoMaxKeyFrameIntervalKey`、profile level（Baseline/Main/High → `AVVideoProfileLevelH264*AutoLevel`）；`startSessionAtSourceTime:kCMTimeZero`。
2. `appendFrame`：
   - `PlatformShared`：`platformHandle` 当作 `CVPixelBufferRef`，`appendPixelBuffer:withPresentationTime:`（`CMTimeMake(presentationIndex * den, num)` 或等价：`CMTimeMake(presentationIndex * frameRate.den, frameRate.num)`）。
   - `CpuRgba`：创建临时 `CVPixelBuffer`（32BGRA），把 RGBA 行拷到 BGRA（注意通道交换），再 append。
3. `end`：`markAsFinished` + `finishWritingWithCompletionHandler`（用信号量/锁同步等待）。
4. `abort`：`cancelWriting`，`std::filesystem::remove(outputPath)`。
5. 等待 `isReadyForMoreMediaData`（短 spin / 小 sleep），避免丢帧。

- [ ] **Step 5: 测试通过**

```bash
cmake --build build --target avf_adapter_test
./build/adapter/avf/avf_adapter_test --gtest_filter='AvfVideoEncoderTest.*'
# 若产物路径不同：
find build -name avf_adapter_test -type f
```

Expected: PASS。

- [ ] **Step 6: Commit**

```bash
git commit --only adapter/avf CMakeLists.txt \
  -m "Add AVFoundation H.264 MP4 encoder adapter."
```

（`--only` 下列出实际新增/修改的具体文件路径。）

---

### Task 4: TgfxVideoFrameSource（零拷贝 + 忽略圆角）

**Files:**
- Create: `adapter/tgfx/include/TgfxVideoFrameSource.h`
- Create: `adapter/tgfx/src/TgfxVideoFrameSource.mm`
- Create: `adapter/tgfx/src/TgfxCVPixelBufferTarget.h` / `.mm`（可选：把 CVPixelBuffer→Surface 从 FrameSource 拆出）
- Create: `adapter/tgfx/tests/TgfxVideoFrameSourceTest.mm`（或 `.cpp`）
- Modify: `adapter/tgfx/CMakeLists.txt` 仅当需要显式列出源时（通常 `add_files_by_extension` 已覆盖）

**Interfaces:**
- Consumes: `VideoFrameSource`；`SceneEvaluator::Evaluate`；`BuildCommands`；`PlayCommands`；`TgfxCanvasAdapter` 模式
- Produces: `motion::TgfxVideoFrameSource`；`renderFrame` 返回 `PlatformShared`（`CVPixelBufferRef` + CF retain/release 函数指针）

- [ ] **Step 1: 写失败测试**

```cpp
TEST(TgfxVideoFrameSourceTest, RendersPlatformSharedIgnoringCornerRadius) {
    Document document;
    auto composition = std::make_unique<Composition>();
    composition->width = 64;
    composition->height = 64;
    composition->duration = 1;
    composition->frameRate = {30, 1};
    composition->backgroundColor = {0, 1, 0, 1};
    composition->cornerRadius = 20.0f;
    // 可选：加一个贴边矩形层，便于确认角上是背景色而非透明
    document.addComposition(std::move(composition));

    TgfxVideoFrameSource source;
    VideoExportOptions options;
    options.width = 64;
    options.height = 64;
    options.frameRate = {30, 1};
    options.outputPath = "/tmp/unused.mp4";
    options.range = {0, 1};
    ASSERT_TRUE(source.prepare(document, document.compositions[0]->id, options).hasValue());
    auto frame = source.renderFrame(0);
    ASSERT_TRUE(frame.hasValue()) << frame.error();
    EXPECT_EQ(frame->storage, VideoFrameStorage::PlatformShared);
    ASSERT_NE(frame->platformHandle, nullptr);
    CVPixelBufferRef buffer = static_cast<CVPixelBufferRef>(frame->platformHandle);
    EXPECT_EQ(CVPixelBufferGetWidth(buffer), 64u);
    EXPECT_EQ(CVPixelBufferGetHeight(buffer), 64u);
    // 锁像素读四角：alpha 应为 255（不透明），且接近绿色背景
    CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    // ... 采样 (0,0) 与 (63,63) BGRA ...
    CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    if (frame->releaseHandle) {
        frame->releaseHandle(frame->platformHandle);
    }
    source.finish();
}
```

四角采样断言写进测试：若仍应用圆角，角上常为透明/黑；强制 `cornerRadius=0` 后应为不透明绿。

- [ ] **Step 2: 跑测试确认失败**

```bash
cmake --build build --target tgfx_adapter_test
./build/adapter/tgfx/tgfx_adapter_test --gtest_filter='TgfxVideoFrameSourceTest.*'
```

Expected: 编译失败（类不存在）。

- [ ] **Step 3: 实现零拷贝帧源**

推荐结构（侵入小于改 `TgfxRenderAdapter`）：

1. 私有 `CVPixelBufferTargetAdapter : TgfxCanvasAdapter`：
   - `setPixelBuffer(CVPixelBufferRef)`
   - `acquireTarget`：用 `CVMetalTextureCacheCreateTextureFromImage` 得到 `MTLTexture`，`Surface::MakeFrom`（`ImageOrigin::TopLeft`），格式与 buffer 一致（`BGRA8Unorm`）
   - `presentTarget`：flush + unlock device
2. `TgfxVideoFrameSource`：
   - `prepare`：保存 `Document*` / `compositionId` / resolved options；创建 Metal device + `CVMetalTextureCache`；按 width/height 建 1–2 个 IOSurface-backed `CVPixelBuffer`（`kCVPixelFormatType_32BGRA`，`kCVPixelBufferMetalCompatibilityKey=true`）
   - `renderFrame(t)`：
     - `SceneEvaluator::Evaluate(*document_, id_, t)`
     - 取池中 buffer，绑定到 adapter
     - `Color bg = state.backgroundColor; bg.a = 1;`
     - `adapter.beginFrame(options.width, options.height, bg, /*cornerRadius=*/0.f)`
     - 若 `options.width/height` 与 `state.viewportWidth/Height` 不同：在 Play 前 `save` + `concatTransform(scale)` + 之后由命令自带 save/restore；或手动包一层 scale（stretch）
     - `PlayCommands(BuildCommands(state), adapter)`
     - `adapter.endFrame()`
     - 返回 `VideoFrame{PlatformShared, handle=buffer, retain=CFRetain, release=CFRelease}`（注意 CF 函数签名用 thin trampoline：`static void Retain(void* p){ CFRetain(p); }`）
   - `finish`：释放 pool / cache / adapter

缩放矩阵：`Mat3::Scale(Vec2{exportW / viewportW, exportH / viewportH})`。

- [ ] **Step 4: 测试通过**

```bash
cmake --build build --target tgfx_adapter_test
./build/adapter/tgfx/tgfx_adapter_test --gtest_filter='TgfxVideoFrameSourceTest.*'
```

Expected: PASS（Metal 不可用则 `GTEST_SKIP`，与现有 adapter 测试一致）。

- [ ] **Step 5: Commit**

```bash
git commit --only adapter/tgfx/include/TgfxVideoFrameSource.h \
  adapter/tgfx/src/TgfxVideoFrameSource.mm \
  adapter/tgfx/tests/TgfxVideoFrameSourceTest.mm \
  # 以及拆出的 Target 文件 \
  -m "Add zero-copy tgfx video frame source ignoring corner radius."
```

---

### Task 5: 端到端库层冒烟（FrameSource + Encoder）

**Files:**
- Create: `adapter/avf/tests/VideoExportIntegrationTest.mm`  
  或放在 `adapter/tgfx/tests/` 并 link `motionstudio_avf_adapter`（二选一；推荐 avf 测试链 tgfx，因导出集成更靠近编码验收）

**Interfaces:**
- Consumes: `VideoExporter`、`TgfxVideoFrameSource`、`AvfVideoEncoder`

- [ ] **Step 1: 集成测试**

```cpp
TEST(VideoExportIntegrationTest, ExportsShortClip) {
    // 64x64、2 帧、绿背景、无图层
    // TgfxVideoFrameSource + AvfVideoEncoder + VideoExporter::Export
    // 断言输出文件存在、AVURLAsset duration > 0
    // 清理临时文件
}
```

CMake：`avf_adapter_test` `target_link_libraries(... motionstudio_tgfx_adapter)`，include tgfx 头路径。

- [ ] **Step 2: 构建并运行**

```bash
cmake --build build --target avf_adapter_test
./build/adapter/avf/avf_adapter_test --gtest_filter='VideoExportIntegrationTest.*'
```

Expected: PASS 或 Metal skip。

- [ ] **Step 3: Commit**

```bash
git commit --only adapter/avf/tests/VideoExportIntegrationTest.mm adapter/avf/CMakeLists.txt \
  -m "Add end-to-end silent MP4 export smoke test."
```

---

### Task 6: Bridge API + 文档

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`（Apple 段声明 `MSVideoExportOptions` + `ms_video_export`）
- Create: `bridge/src/apple/motionstudio_bridge_video_export.mm`
- Modify: `bridge/CMakeLists.txt`：Apple 下 `target_link_libraries(bridge PUBLIC motionstudio_avf_adapter)`（或 PRIVATE）
- Create: `bridge/tests/VideoExportBridgeTest.cpp`（非法 path / null options）
- Modify: `docs/rendering.md` §5：补充 MP4 导出边界（走渲染路径、忽略圆角、可替换 encoder）

**Interfaces:**
- Produces: spec §2.6 的 C API

- [ ] **Step 1: 头文件声明**

在 `motionstudio_bridge.h` 的 canvas/Apple 相关区域旁加入（可用 `#if defined(__APPLE__)` 或与现有 canvas 声明同一无条件——若头文件对全平台可见，则声明始终存在，非 Apple 链接时不提供符号；与项目现有 canvas API 风格对齐）：

```c
typedef struct {
    const char *outputPath;
    int64_t startFrame;
    int64_t endFrame;
    int width;
    int height;
    int frameRateNum;
    int frameRateDen;
    int bitrateBps;
    int keyframeInterval;
    int profile;
} MSVideoExportOptions;

bool ms_video_export(MSDocument *document, uint64_t compositionId,
                     const MSVideoExportOptions *options,
                     bool (*progress)(void *ctx, int64_t completed, int64_t total),
                     void *progressCtx, const volatile int *cancelFlag, char **errorOut);
```

- [ ] **Step 2: 实现 `.mm`**

```objc
bool ms_video_export(...) {
  if (!document || !options || !options->outputPath) { set error; return false; }
  VideoExportOptions o;
  o.outputPath = options->outputPath;
  o.range = { options->startFrame < 0 ? 0 : options->startFrame,
              options->endFrame < 0 ? /* leave 0,0 so Resolve defaults */ 0 : options->endFrame };
  // 若 endFrame<0：保持 range.end<=start 以触发 Resolve 默认整段
  o.width = options->width;
  o.height = options->height;
  o.frameRate = { (uint32_t)options->frameRateNum, (uint32_t)max(options->frameRateDen, 0) };
  o.bitrateBps = options->bitrateBps;
  o.keyframeInterval = options->keyframeInterval;
  o.profile = map profile 0/1/2 → Baseline/Main/High;

  TgfxVideoFrameSource source;
  AvfVideoEncoder encoder;
  auto result = VideoExporter::Export(*doc, EntityId{compositionId}, o, source, encoder,
    [&](VideoExportProgress p) {
      if (cancelFlag && *cancelFlag) return false;
      if (progress) return progress(progressCtx, p.completedFrames, p.totalFrames);
      return true;
    });
  if (!result.hasValue()) { *errorOut = strdup(result.error().c_str()); return false; }
  return true;
}
```

错误字符串分配方式对齐现有 bridge（查找 `ms_string_free` 配套的 `strdup` / 项目 helper，不要混用新分配器）。

- [ ] **Step 3: Bridge 测试**

```cpp
TEST(VideoExportBridgeTest, NullDocumentFails) {
    char *error = nullptr;
    MSVideoExportOptions options{};
    options.outputPath = "/tmp/x.mp4";
    EXPECT_FALSE(ms_video_export(nullptr, 0, &options, nullptr, nullptr, nullptr, &error));
    ASSERT_NE(error, nullptr);
    ms_string_free(error);
}
```

- [ ] **Step 4: 构建 bridge_test + 更新 docs/rendering.md**

在 §5 增加段落：

> **MP4（H.264）导出**走渲染路径：`VideoExporter` 逐帧 `Evaluate → BuildCommands → PlayCommands`，经可替换 `VideoEncoder` 封装。Apple 默认 `TgfxVideoFrameSource`（CVPixelBuffer 零拷贝）+ `AvfVideoEncoder`。导出强制 `cornerRadius=0`、不透明背景；音轨接口预留。Lottie 仍为模型直转例外。

- [ ] **Step 5: 全量相关测试**

```bash
cmake --build build
./build/tests/core_tests --gtest_filter='VideoExporterTest.*'
./build/adapter/avf/avf_adapter_test
./build/adapter/tgfx/tgfx_adapter_test --gtest_filter='TgfxVideoFrameSourceTest.*'
./build/bridge/bridge_test --gtest_filter='VideoExportBridgeTest.*'
```

Expected: PASS（Metal skip 可接受）。

- [ ] **Step 6: Commit**

```bash
git commit --only bridge/include/motionstudio_bridge.h \
  bridge/src/apple/motionstudio_bridge_video_export.mm \
  bridge/CMakeLists.txt \
  bridge/tests/VideoExportBridgeTest.cpp \
  docs/rendering.md \
  -m "Expose MP4 video export through the Apple bridge."
```

---

## Spec Coverage Checklist

| Spec 项 | Task |
|---|---|
| Core 编排 / 可注入 | 1–2 |
| VideoFrame Shared+CPU | 1, 3–4 |
| Options：range/size/fps/bitrate/gop/profile | 2–3, 6 |
| 进度取消 | 2, 6 |
| 无声 + attachAudio 预留 | 1–2 |
| 忽略 cornerRadius / 不透明 | 4 |
| AvfVideoEncoder | 3 |
| 零拷贝 FrameSource | 4 |
| Bridge API | 6 |
| 无 UI / 无 FFmpeg 实现 | 全局约束（不建任务） |
| 测试策略 | 2–6 |

## 自检备注

- 默认码率与 `VideoExporterTest.ExportsDefaultRange...` 中的 `6220800` 绑定；改公式必须同改测试。
- `Expected<void, std::string>` 成功返回 `Expected<void, std::string>()`（见 `Expected.h` void 特化）；Fake `prepare`/`begin`/`end`/`appendFrame` 同此写法，不要写 `return {}` 若编译器歧义。
- Bridge 错误分配必须与 `ms_string_free` 匹配。
- FrameSource 缩放用 `Mat3::Scale(Vec2{...})`。
