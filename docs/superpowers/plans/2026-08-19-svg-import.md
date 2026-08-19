# SVG Import Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用 tgfx `SVGDOM` 把 SVG 转成可插入当前合成的 Core 图层树（`ShapePath` + `VectorNetwork`），一次 undo 整棵撤销。

**Architecture:** 独立静态库 `svg_import`（`src/import/svg`）PUBLIC 链 `core`、PRIVATE 链预编译 tgfx。`BuildSvgLayers` 纯转换；`ImportSvgInto` 用一条 `CompositeCommand`（`ImportImageAssetCommand` + `AddLayerCommand`）写入现有 `Document`。公开头禁止出现 tgfx 类型。

**Tech Stack:** C++17、GoogleTest、预编译 tgfx（须 `TGFX_BUILD_SVG=ON`）、现有 `UndoManager` / `CompositeCommand`。

**Spec:** [`docs/superpowers/specs/2026-08-19-svg-import-design.md`](../specs/2026-08-19-svg-import-design.md)

## Global Constraints

- 不改 Core 模型、schema、bridge、App。
- `motionstudio_core` 不得 `#include` tgfx，也不得依赖 `svg_import`。
- 公开头只有 `include/MotionStudio/import/svg/SvgImporter.h`；禁止 tgfx 类型出库。
- 形状一律 `ShapePath` + `VectorNetwork`，不用 `ShapeRect` / `ShapeEllipse`。
- `<text>` → 点文本 `Text` 层（Task 9）；`<textPath>` / 逐字定位跳过 + diagnostic。
- 不调用 `SVGNode::asPath` / `SVGRenderContext`（私有）。
- Expected 用 `hasValue()` / `error()`，不用 `EXPECT_THROW`。
- 禁止 `dynamic_cast` 与 C++ 异常；SVG 子类用 `tag()` + `static_cast`。
- 禁止 lambda；`if`/`switch` 分支必须 `{}`。
- 代码与注释英语；commit 120 字符内英语、句号结尾。
- 仅 Apple：`if(APPLE)` 下 `add_subdirectory(src/import/svg)`。
- 第一次打开 `TGFX_BUILD_SVG=ON` 会重编 `tgfx.a`，可能数分钟。

## File Map

| 文件 | 职责 |
|---|---|
| `include/MotionStudio/import/svg/SvgImporter.h` | 唯一公开 API |
| `src/import/svg/CMakeLists.txt` | `svg_import` + `svg_import_tests` |
| `src/import/svg/SvgImporter.cpp` | `BuildSvgLayers` / `ImportSvgInto` |
| `src/import/svg/SvgParse.h/.cpp` | 字节 → `SVGDOM` + 视口尺寸 |
| `src/import/svg/SvgLength.h/.cpp` | `SVGLengthContext` 包装 |
| `src/import/svg/SvgPathConvert.h/.cpp` | `tgfx::Path` → `VectorNetwork` |
| `src/import/svg/SvgTransform.h/.cpp` | Matrix 分解 + 中心锚点 |
| `src/import/svg/SvgStyle.h/.cpp` | 继承样式 + Fill/Stroke/Gradient |
| `src/import/svg/SvgWalk.h/.cpp` | 遍历节点建图层树 |
| `src/import/svg/SvgText.h/.cpp` | `<text>` / tspan → `TextContent` |
| `tests/import/svg/SvgImporterTest.cpp` | 库测 |
| `tests/import/svg/fixtures/` | 可选较大夹具；小 SVG 可写在测试字符串里 |
| `adapter/tgfx/tests/SvgImportRenderTest.mm` | 导入后走 Core 管线渲一帧并落 WebP（人工核对） |
| 根 `CMakeLists.txt` | `-DTGFX_BUILD_SVG=ON` + `add_subdirectory` |
| `docs/architecture.md` | 目录树补 `import/svg` |

---

### Task 1: 库骨架、CMake、解析失败与固有尺寸

**Status:** ✅ Done

**Files:**
- Create: `include/MotionStudio/import/svg/SvgImporter.h`
- Create: `src/import/svg/CMakeLists.txt`
- Create: `src/import/svg/SvgImporter.cpp`
- Create: `src/import/svg/SvgParse.h`
- Create: `src/import/svg/SvgParse.cpp`
- Create: `tests/import/svg/SvgImporterTest.cpp`
- Modify: `CMakeLists.txt`（`TGFX_CMAKE_ARGS` 与 `add_subdirectory`）

**Interfaces:**
- Consumes: 无
- Produces: `motion::svg::BuildSvgLayers` / `BuildSvgLayersFromFile`；`ImportSvgInto` / `ImportSvgFileInto` 本 Task 可先返回 `Unexpected("not implemented")`（Task 5 再填）

- [x] **Step 1: 写公开头**

```cpp
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "MotionStudio/common/EntityId.h"
#include "MotionStudio/common/Expected.h"
#include "MotionStudio/model/Asset.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/model/Layer.h"
#include "MotionStudio/undo/UndoManager.h"

namespace motion {
namespace svg {

struct ImportOptions {
    int insertIndex = -1;
    EntityId parentLayerId{};
    std::string rootName{"SVG"};
};

struct Diagnostic {
    enum class Severity { Warning, Error };
    Severity severity = Severity::Warning;
    std::string code;
    std::string message;
    std::string nodeName;
};

struct EmbeddedImage {
    EntityId assetId;
    std::string suggestedFileName;
    std::vector<uint8_t> bytes;
};

struct SvgLayerTree {
    std::vector<std::unique_ptr<Layer>> layers;
    std::vector<Asset> assets;
    std::vector<EmbeddedImage> embeddedImages;
    std::vector<Diagnostic> diagnostics;
    int sourceWidth = 0;
    int sourceHeight = 0;
};

struct ImportResult {
    EntityId rootLayerId;
    std::vector<EntityId> layerIds;
    std::vector<Diagnostic> diagnostics;
    int sourceWidth = 0;
    int sourceHeight = 0;
};

Expected<SvgLayerTree, std::string> BuildSvgLayers(const void *bytes, size_t length,
                                                   const ImportOptions &options = {});
Expected<SvgLayerTree, std::string> BuildSvgLayersFromFile(const std::string &path,
                                                           const ImportOptions &options = {});
Expected<ImportResult, std::string> ImportSvgInto(Document &document, UndoManager &undo,
                                                  EntityId compositionId, const void *bytes,
                                                  size_t length,
                                                  const ImportOptions &options = {});
Expected<ImportResult, std::string> ImportSvgFileInto(Document &document, UndoManager &undo,
                                                      EntityId compositionId,
                                                      const std::string &path,
                                                      const ImportOptions &options = {});

}  // namespace svg
}  // namespace motion
```

- [x] **Step 2: 写会失败的测试**

`tests/import/svg/SvgImporterTest.cpp`：

```cpp
#include <string>

#include <gtest/gtest.h>

#include "MotionStudio/import/svg/SvgImporter.h"

using motion::svg::BuildSvgLayers;
using motion::svg::ImportOptions;

TEST(SvgImporterTest, EmptyBufferFails) {
    const auto result = BuildSvgLayers("", 0);
    ASSERT_FALSE(result.hasValue());
}

TEST(SvgImporterTest, NonSvgRootFails) {
    const std::string xml = "<html></html>";
    const auto result = BuildSvgLayers(xml.data(), xml.size());
    ASSERT_FALSE(result.hasValue());
}

TEST(SvgImporterTest, EmptySvgReportsSourceSizeAndRootGroup) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"100\"></svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().sourceWidth, 200);
    EXPECT_EQ(result.value().sourceHeight, 100);
    ASSERT_EQ(result.value().layers.size(), 1u);
    EXPECT_EQ(result.value().layers[0]->type(), motion::LayerType::Group);
    EXPECT_EQ(result.value().layers[0]->name, "SVG");
}
```

- [x] **Step 3: 跑测试确认失败**

```bash
# 先改 CMake 再编；此时尚无 target 也算失败。加上 target 后未实现应 FAIL。
cmake -B build -G Ninja -DMOTIONSTUDIO_ENABLE_ASAN=ON
cmake --build build --target svg_import_tests
./build/src/import/svg/svg_import_tests --gtest_filter='SvgImporterTest.*'
```

Expected: 链接失败或 `EmptySvgReportsSourceSizeAndRootGroup` FAIL（尚未实现 / 返回 error）。

- [x] **Step 4: CMake 接入**

根 `CMakeLists.txt` 的 `TGFX_CMAKE_ARGS` 改为：

```cmake
set(TGFX_CMAKE_ARGS "-DTGFX_USE_METAL=ON -DTGFX_USE_OPENGL=OFF -DTGFX_USE_PNG_DECODE=ON -DTGFX_USE_PNG_ENCODE=ON -DTGFX_USE_JPEG_DECODE=ON -DTGFX_USE_JPEG_ENCODE=ON -DTGFX_USE_WEBP_DECODE=ON -DTGFX_USE_WEBP_ENCODE=ON -DTGFX_BUILD_SVG=ON")
```

在 `add_subdirectory(src/export/pag)` 之后、`bridge` 之前：

```cmake
add_subdirectory(src/import/svg)
```

`src/import/svg/CMakeLists.txt` 对标 `src/export/pag/CMakeLists.txt`：

```cmake
add_files_by_extension(SVG_IMPORT_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(svg_import STATIC ${SVG_IMPORT_SOURCES})
set_target_properties(svg_import PROPERTIES OUTPUT_NAME motionstudio_svg_import)
add_library(motionstudio_svg_import ALIAS svg_import)
add_source_group(SVG_IMPORT_SOURCES ${CMAKE_CURRENT_LIST_DIR} "Sources")

target_include_directories(svg_import
                           PUBLIC
                           "${PROJECT_SOURCE_DIR}/include"
                           PRIVATE
                           "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${TGFX_INCLUDE_DIR}")

target_link_libraries(svg_import PUBLIC core)
motionstudio_add_tgfx_prebuild(svg_import)
motionstudio_link_prebuilt_tgfx(svg_import)
target_compile_options(svg_import PRIVATE -Wall -Wextra)

include(GoogleTest)
add_files_by_extension(SVG_IMPORT_TEST_SOURCES ".h;.cpp"
    ${PROJECT_SOURCE_DIR}/tests/import/svg
)
add_executable(svg_import_tests ${SVG_IMPORT_TEST_SOURCES})
add_source_group(SVG_IMPORT_TEST_SOURCES ${PROJECT_SOURCE_DIR}/tests/import/svg "Tests")
disable_xcode_target_signing(svg_import_tests)
target_include_directories(svg_import_tests PRIVATE
                           "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(svg_import_tests PRIVATE svg_import GTest::gtest_main)
motionstudio_link_asan_defaults(svg_import_tests)
motionstudio_add_tgfx_prebuild(svg_import_tests)
motionstudio_link_prebuilt_tgfx(svg_import_tests)
gtest_discover_tests(svg_import_tests)
```

- [x] **Step 5: 实现解析 + 空 svg**

`SvgParse.h`：

```cpp
#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "tgfx/svg/SVGDOM.h"

namespace motion {
namespace svg {

struct ParsedSvg {
    std::shared_ptr<tgfx::SVGDOM> dom;
    int sourceWidth = 0;
    int sourceHeight = 0;
};

Expected<ParsedSvg, std::string> ParseSvgBytes(const void *bytes, size_t length);
int ResolveRootSourceSize(const tgfx::SVGRoot &root, float *outWidth, float *outHeight);

}  // namespace svg
}  // namespace motion
```

`ParseSvgBytes`：`length==0` → `"empty svg"`；`Data::MakeWithCopy` + `Stream::MakeFromData` + `SVGDOM::Make`；失败或 `getRoot()` 空 / `tag()!=Svg` → `"invalid svg"`。

固有尺寸（对齐 spec §5.1 / `SVGDOM::getContainerSize`）：

```cpp
// 有 viewBox：viewport = viewBox.size()，再 resolve width/height
// 无 viewBox：viewport = (100,100)，再 resolve width/height
// 任一边 <= 0 → 失败
// sourceWidth/Height = max(1, round(w/h))
```

用公开 `SVGLengthContext`。不要 include `src/svg/`。

`BuildSvgLayers`：解析成功后建一个 `Layer(LayerType::Group)`，`name = options.rootName`，`inPoint=0`，`outPoint=0`（Task 5 写入合成 duration）。推进 `layers`。`ImportSvgInto` / `ImportSvgFileInto` 先 `return Unexpected<std::string>("not implemented");`。

`BuildSvgLayersFromFile`：读文件到 `std::string`，再调 `BuildSvgLayers`；读失败 → `"cannot read file"`。

- [x] **Step 6: 编测通过**

```bash
cmake -B build -G Ninja -DMOTIONSTUDIO_ENABLE_ASAN=ON
cmake --build build --target svg_import_tests
./build/src/import/svg/svg_import_tests --gtest_filter='SvgImporterTest.*'
```

Expected: PASS。若 tgfx 预编译因 `TGFX_BUILD_SVG` 失效，等预编译完成。

- [x] **Step 7: Commit**

```bash
git add include/MotionStudio/import/svg/SvgImporter.h src/import/svg CMakeLists.txt tests/import/svg
git commit -m "Add the svg_import library skeleton and parse failure tests."
```

---

### Task 2: `tgfx::Path` → `VectorNetwork`

**Status:** ✅ Done

**Files:**
- Create: `src/import/svg/SvgPathConvert.h`
- Create: `src/import/svg/SvgPathConvert.cpp`
- Modify: `tests/import/svg/SvgImporterTest.cpp`

**Interfaces:**
- Consumes: Task 1 库
- Produces: `VectorNetwork PathToVectorNetwork(const tgfx::Path &path, bool *usedConic);`

- [x] **Step 1: 写失败测试**

测试文件可 `#include "SvgPathConvert.h"`（PRIVATE src 目录）。

```cpp
#include "tgfx/core/Path.h"
#include "SvgPathConvert.h"

TEST(SvgPathConvertTest, ClosedRectHasFourEdges) {
    tgfx::Path path;
    path.addRect(tgfx::Rect::MakeXYWH(10, 20, 40, 30));
    bool usedConic = false;
    const motion::VectorNetwork network = motion::svg::PathToVectorNetwork(path, &usedConic);
    EXPECT_FALSE(usedConic);
    EXPECT_EQ(network.vertices.size(), 4u);
    EXPECT_EQ(network.edges.size(), 4u);
    EXPECT_EQ(network.vertices.front().id, 1u);
    EXPECT_EQ(network.edges.front().id, 1u);
    EXPECT_EQ(network.vertices.front().mirrorMode, motion::VertexMirrorMode::None);
}

TEST(SvgPathConvertTest, OpenLineHasOneEdge) {
    tgfx::Path path;
    path.moveTo(0, 0);
    path.lineTo(10, 0);
    bool usedConic = false;
    const motion::VectorNetwork network = motion::svg::PathToVectorNetwork(path, &usedConic);
    EXPECT_EQ(network.vertices.size(), 2u);
    EXPECT_EQ(network.edges.size(), 1u);
}
```

- [x] **Step 2: 跑测试确认失败**

```bash
cmake --build build --target svg_import_tests
./build/src/import/svg/svg_import_tests --gtest_filter='SvgPathConvertTest.*'
```

Expected: FAIL（未声明 `PathToVectorNetwork`）。

- [x] **Step 3: 实现转换**

按 spec §6.2 迭代 `path`：

- Move：新轮廓起点，`AllocVertexId` 风格从 1 递增
- Line：边，切线 0
- Cubic：`startTangent = c1 - p0`，`endTangent = c2 - p3`
- Quad：升阶 `C1 = P0 + 2/3(Q-P0)`，`C2 = P2 + 2/3(Q-P2)`
- Conic：升阶近似为 1 段 cubic（`usedConic=true`）。公式：把 conic 当加权二次，先转 quad（weight≈1）或用标准 conic→cubic（`w` 为权重，`q1 = (p0 + 2w q)/(1+2w)` 一类）；实现选一种并在函数注释写公式
- Close：连回本轮廓 Move 点；终点重合则不复制顶点；零长边丢弃
- 自环丢弃；不同轮廓不合并重合点

`Iterator` 的 Line/Cubic `points[0]` 是当前点，`points[1..]` 是后续控制点/终点（以 tgfx `Path.h` Segment 注释为准）。

- [x] **Step 4: 跑测试确认通过**

```bash
./build/src/import/svg/svg_import_tests --gtest_filter='SvgPathConvertTest.*'
```

Expected: PASS。

- [x] **Step 5: Commit**

```bash
git add src/import/svg/SvgPathConvert.h src/import/svg/SvgPathConvert.cpp tests/import/svg/SvgImporterTest.cpp
git commit -m "Convert tgfx paths into editable VectorNetwork geometry."
```

---

### Task 3: 基本形状走 `ShapePath` + 中心锚点

**Status:** ✅ Done

**Files:**
- Create: `src/import/svg/SvgLength.h/.cpp`
- Create: `src/import/svg/SvgWalk.h/.cpp`
- Create: `src/import/svg/SvgStyle.h/.cpp`（本 Task 只做本节点纯色 fill/stroke，继承下一 Task）
- Create: `src/import/svg/SvgTransform.h/.cpp`（本 Task 实现 AABB 中心锚点；旋转缩放下一 Task）
- Modify: `src/import/svg/SvgImporter.cpp`
- Modify: `tests/import/svg/SvgImporterTest.cpp`

**Interfaces:**
- Consumes: `PathToVectorNetwork`、`ParseSvgBytes`
- Produces: `BuildSvgLayers` 能输出 Shape 层；`AssignCenterAnchors(std::vector<std::unique_ptr<Layer>> &layers)`

- [x] **Step 1: 写失败测试**

```cpp
TEST(SvgImporterTest, PathFillStrokeBecomesShapePath) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<path d=\"M0 0 H10 V10 H0 Z\" fill=\"#ff0000\" stroke=\"#0000ff\" stroke-width=\"4\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().layers.size(), 2u);
    const motion::Layer *shape = result.value().layers[1].get();
    EXPECT_EQ(shape->type(), motion::LayerType::Shape);
    EXPECT_EQ(shape->parentId, result.value().layers[0]->id);
    auto *content = static_cast<motion::ShapeContent *>(shape->content.get());
    ASSERT_NE(content->geometry, nullptr);
    EXPECT_EQ(content->geometry->type(), motion::ShapeType::Path);
    auto *path = static_cast<motion::ShapePath *>(content->geometry.get());
    const motion::VectorNetwork network = path->path.staticValue();
    EXPECT_FALSE(network.edges.empty());
    ASSERT_EQ(shape->styles.size(), 2u);
    auto *fill = static_cast<motion::FillStyle *>(shape->styles[0].get());
    EXPECT_EQ(fill->type(), motion::LayerStyleType::Fill);
    EXPECT_NEAR(fill->color.staticValue().r, 1.f, 1e-3f);
    auto *stroke = static_cast<motion::StrokeStyle *>(shape->styles[1].get());
    EXPECT_NEAR(stroke->width.staticValue(), 4.f, 1e-3f);
    EXPECT_EQ(stroke->position, motion::StrokePosition::Center);
}

TEST(SvgImporterTest, RectCircleEllipseAreVectorNetworks) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"200\">"
        "<rect x=\"10\" y=\"20\" width=\"40\" height=\"30\" fill=\"#00ff00\"/>"
        "<circle cx=\"50\" cy=\"50\" r=\"10\" fill=\"#000000\"/>"
        "<ellipse cx=\"80\" cy=\"40\" rx=\"20\" ry=\"10\" fill=\"#000000\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().layers.size(), 4u);
    for (size_t i = 1; i < result.value().layers.size(); ++i) {
        auto *content =
            static_cast<motion::ShapeContent *>(result.value().layers[i]->content.get());
        ASSERT_EQ(content->geometry->type(), motion::ShapeType::Path);
    }
}

TEST(SvgImporterTest, AxisAlignedRectUsesCenterAnchor) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"200\">"
        "<rect x=\"10\" y=\"20\" width=\"40\" height=\"30\" fill=\"#000000\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    const motion::Layer *shape = result.value().layers[1].get();
    const motion::Vec2 anchor = shape->transform.anchorPoint.staticValue();
    const motion::Vec2 position = shape->transform.position.staticValue();
    EXPECT_NEAR(anchor.x, 30.f, 1e-3f);
    EXPECT_NEAR(anchor.y, 35.f, 1e-3f);
    EXPECT_NEAR(position.x, 30.f, 1e-3f);
    EXPECT_NEAR(position.y, 35.f, 1e-3f);
}

TEST(SvgImporterTest, LineIsOpenStrokeOnly) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<line x1=\"0\" y1=\"0\" x2=\"10\" y2=\"0\" stroke=\"#000000\" stroke-width=\"2\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    const motion::Layer *shape = result.value().layers[1].get();
    ASSERT_EQ(shape->styles.size(), 1u);
    EXPECT_EQ(shape->styles[0]->type(), motion::LayerStyleType::Stroke);
}

```

本 Task **先跳过** `text` / `textPath`（不写 `text.skipped`；Task 9 再导入）。不要加 `TextIsSkipped`。

- [x] **Step 2: 跑测试确认失败**

```bash
./build/src/import/svg/svg_import_tests --gtest_filter='SvgImporterTest.PathFillStroke*:SvgImporterTest.Rect*:SvgImporterTest.Axis*:SvgImporterTest.Line*'
```

Expected: FAIL（只有根 Group）。

- [x] **Step 3: 实现 walk + 形状 + 默认样式 + 中心锚点**

`SvgWalk` 深度优先：

- `defs` / gradient / clipPath / mask / filter / pattern / marker / stop / style / title / desc / metadata：不建层
- `text` / `textPath` / `tspan` / `TextLiteral`：本 Task return（Task 9 再导入）
- `G` / 嵌套 `Svg`：`LayerType::Group`，再 walk children
- `Path` / `Rect` / `Circle` / `Ellipse` / `Line` / `Polygon` / `Polyline`：用 `SVGLengthContext`（viewport = 根固有尺寸）resolve 后建 `tgfx::Path`（`getShapePath()` / `addRect` / `addOval` / `moveTo+lineTo` / points），`PathToVectorNetwork`，挂 `ShapePath`
- 零面积且无 stroke：skip + `shape.empty`
- `display=none`：本 Task 可先不做（Task 6）
- 命名：`id` → class 第一段 → `Path`/`Rectangle`/`Ellipse`/`Line`/`Polygon`/`Group`/`Text`

默认 ComputedStyle（无继承）：fill 黑、stroke none。本节点 `getFill()` / `getStroke()` 为 Value 则覆盖。`fill=none` 不加 Fill；`stroke=none` 不加 Stroke。`StrokePosition::Center`。

`AssignCenterAnchors` 后序：Shape 用顶点 AABB 中心；空盒 → anchor 0、position=translation。无旋转时 `position = translation + anchor`。

根 Group 永远先 push，子层 `parentId = root.id`。

`SvgStyle.cpp` / `SvgTransform.cpp` 本 Task 只放上述最小函数，避免把未测的渐变/剪切写进去。

- [x] **Step 4: 跑测试确认通过**

```bash
./build/src/import/svg/svg_import_tests --gtest_filter='SvgImporterTest.*:SvgPathConvertTest.*'
```

Expected: PASS。

- [x] **Step 5: Commit**

```bash
git add src/import/svg tests/import/svg
git commit -m "Import SVG primitives as center-anchored VectorNetwork shapes."
```

---

### Task 4: Group、节点 transform、viewBox

**Status:** ✅ Done

**Files:**
- Modify: `src/import/svg/SvgTransform.cpp`
- Modify: `src/import/svg/SvgWalk.cpp`
- Modify: `tests/import/svg/SvgImporterTest.cpp`

**Interfaces:**
- Consumes: `AssignCenterAnchors`、walk
- Produces: `DecomposedTransform DecomposeSvgMatrix(const tgfx::Matrix &matrix);`  
  `void ApplyResidualBake(Layer &layer, const tgfx::Matrix &residual);`  
  字段：`Vec2 translation`、`Vec2 scale`、`float rotationDegrees`、`bool hasShear`、`bool singular`

tgfx `Matrix` 行主序：

```
| scaleX  skewX  transX |
| skewY   scaleY transY |
```

对应 spec 的 `a=scaleX, b=skewY, c=skewX, d=scaleY, tx, ty`。

- [x] **Step 1: 写失败测试**

```cpp
TEST(SvgImporterTest, GroupTransformParentsChild) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"200\">"
        "<g transform=\"translate(10 20) rotate(90)\">"
        "<rect x=\"0\" y=\"0\" width=\"20\" height=\"10\" fill=\"#000000\"/>"
        "</g></svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    ASSERT_GE(result.value().layers.size(), 3u);
    const motion::Layer *group = result.value().layers[1].get();
    const motion::Layer *shape = result.value().layers[2].get();
    EXPECT_EQ(group->type(), motion::LayerType::Group);
    EXPECT_EQ(shape->parentId, group->id);
    EXPECT_NEAR(group->transform.rotation.staticValue(), 90.f, 1e-2f);
    const motion::Mat3 local = group->localTransform(0);
    const motion::Vec2 mapped = local.transformPoint({0.f, 0.f});
    EXPECT_NEAR(mapped.x, 10.f, 0.05f);
    EXPECT_NEAR(mapped.y, 20.f, 0.05f);
}

TEST(SvgImporterTest, ViewBoxDoesNotResizeCallerComposition) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"50\" "
        "viewBox=\"10 20 80 40\"></svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().sourceWidth, 100);
    EXPECT_EQ(result.value().sourceHeight, 50);
    const motion::Vec2 scale = result.value().layers[0]->transform.scale.staticValue();
    EXPECT_GT(scale.x, 0.f);
}
```

`localTransform` 必须等于 SVG 矩阵（允许 1e-3）。中心锚点补偿后用 `layer->localTransform(0)` 比直接比 position 更稳。

- [x] **Step 2: 跑测试确认失败**

Expected: Group 无 rotation 或没有中间 Group → FAIL。

- [x] **Step 3: 实现分解 + viewBox**

`DecomposeSvgMatrix`：

```
scaleX = hypot(a, b)
det = a*d - b*c
scaleY = hypot(c, d) * (det < 0 ? -1 : 1)
rotation = atan2(b, a) * 180/pi
```

`|dot(normalize(a,b), normalize(c,d))| > 1e-3` → `hasShear`。`|det| < 1e-8` → `singular`。

walk 每个 `SVGTransformableNode`：取 `getTransform()`。singular：整矩阵 bake 进几何（Shape：变换 Network 顶点与切线；Group：记到子层几何，本组单位矩阵）。hasShear：bake `M * inverse(T*R*S)`。然后写 translation/R/S，再 `AssignCenterAnchors`。

Group AABB：子层局部盒四角经子层 `localTransform(0)` 变到组空间后取并。

viewBox：根 Group 的矩阵 = `ComputeViewboxMatrix` 同语义（meet/slice + 对齐，缺省 xMidYMid meet）。重实现，不调 protected 方法。再与根节点自身 `transform` 左乘。

opacity：节点 `getOpacity()` 为 Value → `transform.opacity`。

- [x] **Step 4: 跑测试确认通过**

```bash
./build/src/import/svg/svg_import_tests --gtest_filter='SvgImporterTest.*'
```

Expected: PASS。

- [x] **Step 5: Commit**

```bash
git add src/import/svg tests/import/svg
git commit -m "Preserve SVG group transforms and viewBox on imported layers."
```

---

### Task 5: `ImportSvgInto` + 一次 undo

**Status:** ✅ Done

**Files:**
- Modify: `src/import/svg/SvgImporter.cpp`
- Modify: `tests/import/svg/SvgImporterTest.cpp`

**Interfaces:**
- Consumes: `BuildSvgLayers`、`AddLayerCommand`、`ImportImageAssetCommand`、`CompositeCommand`、`UndoManager::execute`
- Produces: 完整 `ImportSvgInto` / `ImportSvgFileInto`

- [x] **Step 1: 写失败测试**

```cpp
TEST(SvgImporterTest, ImportIntoExistingCompositionIsUndoable) {
    motion::Document document;
    auto composition = std::make_unique<motion::Composition>();
    composition->width = 640;
    composition->height = 480;
    composition->duration = 90;
    composition->frameRate = {30, 1};
    const motion::EntityId compositionId = composition->id;
    document.addComposition(std::move(composition));

    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"80\" height=\"40\">"
        "<rect x=\"0\" y=\"0\" width=\"10\" height=\"10\" fill=\"#000000\"/>"
        "</svg>";
    motion::UndoManager undo;
    const auto imported = motion::svg::ImportSvgInto(document, undo, compositionId, svg.data(),
                                                     svg.size());
    ASSERT_TRUE(imported.hasValue());
    motion::Composition *host = document.entityIndex().findComposition(compositionId);
    ASSERT_NE(host, nullptr);
    EXPECT_EQ(host->width, 640);
    EXPECT_EQ(host->height, 480);
    EXPECT_EQ(host->duration, 90);
    EXPECT_GE(host->layers.size(), 2u);
    EXPECT_EQ(host->layers.back()->outPoint, 90);
    EXPECT_EQ(imported.value().rootLayerId, host->layers[host->layers.size() - 2]->id);

    undo.undo(document);
    EXPECT_TRUE(host->layers.empty());
    EXPECT_EQ(host->width, 640);
}

TEST(SvgImporterTest, MissingCompositionFailsWithoutMutation) {
    motion::Document document;
    motion::UndoManager undo;
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\"></svg>";
    const auto imported =
        motion::svg::ImportSvgInto(document, undo, motion::EntityId{}, svg.data(), svg.size());
    ASSERT_FALSE(imported.hasValue());
    EXPECT_TRUE(document.compositions.empty());
}
```

根 Group 用 `insertIndex` 插入；随后各层依次插在根之后，保持底→顶文档序。`parentLayerId` 有效则 `setParent`，成环 → Unexpected 且不 `execute`。

- [x] **Step 2: 跑测试确认失败**

Expected: FAIL（`not implemented`）。

- [x] **Step 3: 实现插入**

```cpp
Expected<ImportResult, std::string> ImportSvgInto(...) {
    auto built = BuildSvgLayers(bytes, length, options);
    if (!built.hasValue()) {
        return Unexpected<std::string>(built.error());
    }
    Composition *composition = document.entityIndex().findComposition(compositionId);
    if (composition == nullptr) {
        return Unexpected<std::string>("composition not found");
    }
    SvgLayerTree tree = std::move(built.value());
    if (tree.layers.empty()) {
        return Unexpected<std::string>("empty layer tree");
    }
    if (options.parentLayerId.isValid()) {
        if (!tree.layers.front()->setParent(options.parentLayerId, document)) {
            return Unexpected<std::string>("parent cycle");
        }
    }
    for (auto &layer : tree.layers) {
        layer->inPoint = 0;
        layer->outPoint = composition->duration;
    }
    ImportResult out;
    out.sourceWidth = tree.sourceWidth;
    out.sourceHeight = tree.sourceHeight;
    out.diagnostics = tree.diagnostics;
    out.rootLayerId = tree.layers.front()->id;
    auto composite = std::make_unique<CompositeCommand>("Import SVG");
    for (Asset &asset : tree.assets) {
        composite->add(std::make_unique<ImportImageAssetCommand>(asset));
    }
    int index = options.insertIndex;
    for (size_t i = 0; i < tree.layers.size(); ++i) {
        out.layerIds.push_back(tree.layers[i]->id);
        composite->add(std::make_unique<AddLayerCommand>(compositionId, std::move(tree.layers[i]),
                                                         index));
        if (index >= 0) {
            index += 1;
        }
    }
    undo.execute(document, std::move(composite));
    return out;
}
```

`ImportSvgFileInto`：读文件再转调。

- [x] **Step 4: 跑测试确认通过**

```bash
./build/src/import/svg/svg_import_tests --gtest_filter='SvgImporterTest.Import*:SvgImporterTest.Missing*'
```

Expected: PASS。

- [x] **Step 5: Commit**

```bash
git add src/import/svg/SvgImporter.cpp tests/import/svg/SvgImporterTest.cpp
git commit -m "Insert imported SVG layers into the current composition with undo."
```

---

### Task 6: 样式继承、dash、display / visibility

**Status:** ✅ Done

**Files:**
- Modify: `src/import/svg/SvgStyle.cpp`
- Modify: `src/import/svg/SvgWalk.cpp`
- Modify: `tests/import/svg/SvgImporterTest.cpp`

**Interfaces:**
- Consumes: walk
- Produces: `ComputedStyle ResolveStyle(const tgfx::SVGNode &node, const ComputedStyle &parent);`  
  可继承字段按 spec §7.1（含 font-* / text-anchor）；Unspecified 用 parent；Inherit 用 parent；Value 用本节点。Task 9 才消费字体字段。

- [x] **Step 1: 写失败测试**

```cpp
TEST(SvgImporterTest, FillInheritsFromGroup) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<g fill=\"#ff0000\"><rect x=\"0\" y=\"0\" width=\"10\" height=\"10\"/></g>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    const motion::Layer *shape = result.value().layers.back().get();
    auto *fill = static_cast<motion::FillStyle *>(shape->styles[0].get());
    EXPECT_NEAR(fill->color.staticValue().r, 1.f, 1e-3f);
    EXPECT_EQ(result.value().layers[1]->styles.size(), 0u);
}

TEST(SvgImporterTest, DisplayNoneDropsSubtree) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<g display=\"none\"><rect x=\"0\" y=\"0\" width=\"10\" height=\"10\" fill=\"#000\"/></g>"
        "<rect x=\"20\" y=\"0\" width=\"10\" height=\"10\" fill=\"#000\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().layers.size(), 2u);
}

TEST(SvgImporterTest, HiddenStaysInvisible) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<rect x=\"0\" y=\"0\" width=\"10\" height=\"10\" fill=\"#000\" visibility=\"hidden\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    EXPECT_FALSE(result.value().layers.back()->visible);
}

TEST(SvgImporterTest, StrokeDashIsWarned) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<line x1=\"0\" y1=\"0\" x2=\"10\" y2=\"0\" stroke=\"#000\" stroke-dasharray=\"2 2\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    bool found = false;
    for (const auto &d : result.value().diagnostics) {
        if (d.code == "stroke.dash") {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}
```

- [x] **Step 2: 跑测试确认失败**

Expected: 继承 fill 仍是默认黑 → FAIL。

- [x] **Step 3: 实现**

walk 带 `ComputedStyle parent`。`display=none` 整棵跳过。`visibility=hidden/collapse` → `visible=false`。`stroke-dasharray` 非空 → `stroke.dash`（每节点一次），描边仍导入为实线。`fill-opacity`/`stroke-opacity` 乘进 `Color.a`。`currentColor` 用继承 `color`。linecap/join/miter/fill-rule 按 spec 表映射。未知标签 → `tag.unknown`。

- [x] **Step 4: 跑测试确认通过**

```bash
./build/src/import/svg/svg_import_tests --gtest_filter='SvgImporterTest.*'
```

Expected: PASS。

- [x] **Step 5: Commit**

```bash
git add src/import/svg tests/import/svg
git commit -m "Resolve inherited SVG paints and skip hidden subtrees."
```

---

### Task 7: 线性 / 径向渐变与 `<use>`

**Files:**
- Modify: `src/import/svg/SvgStyle.cpp`
- Modify: `src/import/svg/SvgWalk.cpp`
- Modify: `tests/import/svg/SvgImporterTest.cpp`

**Interfaces:**
- Consumes: `nodeIDMapper()`、`ComputedStyle`
- Produces: `bool TryMapGradient(const tgfx::SVGPaint &paint, const tgfx::SVGIDMapper &mapper, const motion::Vec2 &boundsMin, const motion::Vec2 &boundsSize, GradientPaint *out, std::vector<Diagnostic> *diagnostics);`

- [ ] **Step 1: 写失败测试**

```cpp
TEST(SvgImporterTest, LinearGradientMapsToPaint) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<defs><linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"0\">"
        "<stop offset=\"0\" stop-color=\"#ff0000\"/>"
        "<stop offset=\"1\" stop-color=\"#0000ff\"/>"
        "</linearGradient></defs>"
        "<rect x=\"0\" y=\"0\" width=\"100\" height=\"20\" fill=\"url(#g)\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    const motion::Layer *shape = result.value().layers.back().get();
    auto *fill = static_cast<motion::FillStyle *>(shape->styles[0].get());
    EXPECT_EQ(fill->paintMode, motion::StylePaintMode::Gradient);
    EXPECT_EQ(fill->gradient.type, motion::GradientType::Linear);
    ASSERT_GE(fill->gradient.stops.size(), 2u);
}

TEST(SvgImporterTest, UseExpandsWithoutDefsLayer) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<defs><rect id=\"r\" x=\"0\" y=\"0\" width=\"10\" height=\"10\" fill=\"#0f0\"/></defs>"
        "<use href=\"#r\" x=\"5\" y=\"6\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    EXPECT_GE(result.value().layers.size(), 2u);
    for (const auto &layer : result.value().layers) {
        EXPECT_NE(layer->name, "defs");
    }
}
```

- [ ] **Step 2: 跑测试确认失败**

Expected: fill 不是 Gradient / use 无层 → FAIL。

- [ ] **Step 3: 实现**

`url(#id)`：mapper 查找，最多 8 步 `href` 链。`<2` 个 stop → `gradient.stops` 并跳过该 paint。objectBoundingBox：比例 × 形状 AABB 再换成左上角空间（`start -= bounds.min` 已含在「比例×size」里，start 存在 AABB 空间即 `(0,0)=bounds.min`）。userSpaceOnUse：px 减 `bounds.min`。径向：`end = start + (r,0)`。`fx/fy` ≠ 圆心 → `gradient.focal`。repeat/reflect → `gradient.spread`。`gradientTransform` 乘到 start/end。pattern / 找不到 → `paint.unresolved`。

`<use>`：本地 IRI，深度>32 或 id 栈环 → `use.cycle` / `use.missing`。展开目标为子树，外包 Group，矩阵 `T(x,y)*use.transform`。

- [ ] **Step 4: 跑测试确认通过**

Expected: PASS。

- [ ] **Step 5: Commit**

```bash
git add src/import/svg tests/import/svg
git commit -m "Map SVG gradients and expand use references into layers."
```

---

### Task 8: Image、clip-path、剩余 diagnostic、文档

**Files:**
- Modify: `src/import/svg/SvgWalk.cpp`
- Modify: `src/import/svg/SvgStyle.cpp`
- Modify: `tests/import/svg/SvgImporterTest.cpp`
- Modify: `docs/architecture.md`（目录树加 `import/svg`）

**Interfaces:**
- Consumes: `ImportSvgInto`（image 的 asset 命令已在 Task 5 接好）
- Produces: data URI → `Image` + `Asset` + `embeddedImages`；简单 clip → `Layer.masks[0]`

- [ ] **Step 1: 写失败测试**

用 1×1 PNG 的最小 data URI（或 tgfx 能解的红像素）。若解码依赖 codec 开关，当前 `TGFX_CMAKE_ARGS` 已开 PNG decode。

```cpp
TEST(SvgImporterTest, DataUriImageCreatesAsset) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\">"
        "<image href=\"data:image/png;base64,"
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==\""
        " width=\"16\" height=\"16\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    bool hasImage = false;
    for (const auto &layer : result.value().layers) {
        if (layer->type() == motion::LayerType::Image) {
            hasImage = true;
        }
    }
    EXPECT_TRUE(hasImage);
    EXPECT_FALSE(result.value().assets.empty());
    EXPECT_FALSE(result.value().embeddedImages.empty());
}

TEST(SvgImporterTest, ExternalImageIsSkipped) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\">"
        "<image href=\"photo.png\" width=\"16\" height=\"16\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    bool found = false;
    for (const auto &d : result.value().diagnostics) {
        if (d.code == "image.external") {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(SvgImporterTest, SimpleClipPathBecomesMask) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<defs><clipPath id=\"c\"><rect x=\"0\" y=\"0\" width=\"10\" height=\"10\"/></clipPath></defs>"
        "<rect x=\"0\" y=\"0\" width=\"20\" height=\"20\" fill=\"#000\" clip-path=\"url(#c)\"/>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().layers.back()->masks.size(), 1u);
}
```

再补：`mask=` → `mask.skipped`；`filter=` → `filter.skipped`；复杂 clip（两个子形状）→ `clip.unsupported`。

- [ ] **Step 2: 跑测试确认失败**

Expected: FAIL。

- [ ] **Step 3: 实现**

`<image>` data URI：`SVGImage::LoadImage`（公开静态方法）。失败 → `image.decode`。成功则 `Asset`（`path = "assets/<hexId>.png"`，宽高来自图像），`embeddedImages.bytes` 用 PNG 编码或原始 data URI 解码缓冲。`ImageContent.size` = resolve 后的元素 w/h。`preserveAspectRatio` 的 `none` → `ImageScaleMode::Stretch`，否则 `LetterBox`。`T(x,y)*transform` 再走 §5.2。外部 href → 不建层 + `image.external`。

clip：单个可转 Network 的子形状 → `masks[0]`，`MaskMode::Add`。多子 / 过深 use → `clip.unsupported`。`mask` 属性 → `mask.skipped`。`filter` → `filter.skipped`。

`docs/architecture.md` 目录树增加：

```
│   └── import/svg/                 # SVG → Layer 树（svg_import，链 tgfx）
```

- [ ] **Step 4: 全量该二进制测试**

```bash
cmake --build build --target svg_import_tests
./build/src/import/svg/svg_import_tests
```

Expected: 全部 PASS。

- [ ] **Step 5: Commit**

```bash
git add src/import/svg tests/import/svg docs/architecture.md
git commit -m "Import SVG images and simple clip paths into Core layers."
```

---

### Task 9: `<text>` → 点文本 Text 层

**Files:**
- Create: `src/import/svg/SvgText.h`
- Create: `src/import/svg/SvgText.cpp`
- Modify: `src/import/svg/SvgWalk.cpp`（走 `text` / 同样式 tspan；`textPath` diagnostic）
- Modify: `src/import/svg/SvgStyle.cpp`（ComputedStyle 收 `font-family/size/style/weight`、`text-anchor`）
- Modify: `src/import/svg/SvgTransform.cpp`（Text AABB + 基线补偿，见 spec §8.3）
- Modify: `tests/import/svg/SvgImporterTest.cpp`

**Interfaces:**
- Consumes: `SVGText` / `SVGTSpan` / `SVGTextLiteral`、`SVGText::getTextChildren()`、Font 度量（tgfx `Font` + `TextBlob`）
- Produces: `LayerType::Text` + `TextContent`（`boxTextMode = false`）

- [ ] **Step 1: 写失败测试**

```cpp
TEST(SvgImporterTest, TextBecomesPointTextLayer) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"100\">"
        "<text x=\"20\" y=\"40\" font-size=\"24\" fill=\"#333333\">hello</text>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.value().layers.size(), 2u);
    const motion::Layer *text = result.value().layers[1].get();
    EXPECT_EQ(text->type(), motion::LayerType::Text);
    auto *content = static_cast<motion::TextContent *>(text->content.get());
    EXPECT_EQ(content->text.staticValue(), "hello");
    EXPECT_FALSE(content->boxTextMode);
    EXPECT_NEAR(content->fontSize, 24.f, 1e-3f);
    EXPECT_EQ(content->align, motion::TextAlign::Left);
    ASSERT_FALSE(text->styles.empty());
    EXPECT_EQ(text->styles[0]->type(), motion::LayerStyleType::Fill);
}

TEST(SvgImporterTest, TextAnchorMiddleMapsToCenter) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"100\">"
        "<text x=\"100\" y=\"50\" text-anchor=\"middle\" font-size=\"16\">ab</text>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    auto *content =
        static_cast<motion::TextContent *>(result.value().layers[1]->content.get());
    EXPECT_EQ(content->align, motion::TextAlign::Center);
}

TEST(SvgImporterTest, SameStyleTspanConcatenates) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"100\">"
        "<text x=\"0\" y=\"20\" font-size=\"16\">foo<tspan>bar</tspan></text>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.value().layers.size(), 2u);
    auto *content =
        static_cast<motion::TextContent *>(result.value().layers[1]->content.get());
    EXPECT_EQ(content->text.staticValue(), "foobar");
}

TEST(SvgImporterTest, DifferentFillTspanSplitsLayer) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"200\" height=\"100\">"
        "<text x=\"0\" y=\"20\" font-size=\"16\" fill=\"#000000\">"
        "ab<tspan fill=\"#ff0000\">cd</tspan></text>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    int textCount = 0;
    for (const auto &layer : result.value().layers) {
        if (layer->type() == motion::LayerType::Text) {
            textCount += 1;
        }
    }
    EXPECT_EQ(textCount, 2);
}

TEST(SvgImporterTest, TextPathIsSkipped) {
    const std::string svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
        "<defs><path id=\"p\" d=\"M0 50 H100\"/></defs>"
        "<text><textPath href=\"#p\">curve</textPath></text>"
        "</svg>";
    const auto result = BuildSvgLayers(svg.data(), svg.size());
    ASSERT_TRUE(result.hasValue());
    bool found = false;
    for (const auto &d : result.value().diagnostics) {
        if (d.code == "textPath.skipped") {
            found = true;
        }
    }
    EXPECT_TRUE(found);
    for (const auto &layer : result.value().layers) {
        EXPECT_NE(layer->type(), motion::LayerType::Text);
    }
}
```

- [ ] **Step 2: 跑测试确认失败**

```bash
./build/src/import/svg/svg_import_tests --gtest_filter='SvgImporterTest.Text*:SvgImporterTest.SameStyle*'
```

Expected: FAIL（text 仍被 walk 跳过）。

- [ ] **Step 3: 实现**

按 spec §8.3：`CollectTextRuns` 深度优先拼字面量；遇到异样式或自带 x/y 的 tspan 则 flush 当前 run 再开新层。`M = getTransform() * T(x0+dx0-alignX, y0+dy0-ascent)` 再走 §5.2。`fontStyle`：`Bold` / `Italic` / `Bold Italic` / `""`。空字面量不建层。

- [ ] **Step 4: 跑测试确认通过**

```bash
./build/src/import/svg/svg_import_tests --gtest_filter='SvgImporterTest.*'
```

Expected: PASS。

- [ ] **Step 5: Commit**

```bash
git add src/import/svg tests/import/svg
git commit -m "Import SVG text nodes as editable point text layers."
```

---

### Task 10: 导入后渲染对照（WebP，人工核对）

对标 `adapter/tgfx/tests/ColorSourceEffectTest.mm`：Metal 不可用则 `GTEST_SKIP`；`readPixels` 后用 `SaveWebp` + `OutputPath` 写到 `adapter/tgfx/tests/out/`（已在 `/**/tests/out` gitignore）。**不**做像素对拍门禁，只做「画面非空」冒烟，便于打开 WebP 和源 SVG 对照。

依赖 Task 5（`ImportSvgInto`）、Task 3–7 的形状/组/样式/渐变，以及 Task 9 的文本。放在 `tgfx_adapter_test` 里复用现成 GPU / `TgfxRenderAdapter` / `SaveWebp`，避免 `svg_import_tests` 再链一层 adapter。

**Files:**
- Create: `tests/import/svg/fixtures/kitchen_sink.svg`
- Create: `adapter/tgfx/tests/SvgImportRenderTest.mm`
- Modify: `src/import/svg/CMakeLists.txt`（`tgfx_adapter_test` 链 `svg_import`）

**Interfaces:**
- Consumes: `ImportSvgInto`、`SceneEvaluator::Evaluate`、`BuildCommands`、`PlayCommands`、`TgfxRenderAdapter`、`tgfx_test::SaveWebp` / `OutputPath`、`tgfx::SVGDOM::render`（仅对照参考图）
- Produces: `adapter/tgfx/tests/out/SvgImport_KitchenSink_Imported.webp`、`SvgImport_KitchenSink_SvgDom.webp`

- [ ] **Step 1: 写夹具 SVG**

`tests/import/svg/fixtures/kitchen_sink.svg`（256×256，覆盖本阶段已映射能力）：

```xml
<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <defs>
    <linearGradient id="grad" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0" stop-color="#ff7a18"/>
      <stop offset="1" stop-color="#0055ff"/>
    </linearGradient>
  </defs>
  <rect x="0" y="0" width="256" height="256" fill="#f2f2f2"/>
  <rect x="16" y="16" width="80" height="48" fill="#e03131"/>
  <circle cx="200" cy="48" r="28" fill="#1971c2"/>
  <ellipse cx="64" cy="120" rx="36" ry="20" fill="none" stroke="#2f9e44" stroke-width="6"/>
  <g transform="translate(160 140) rotate(30)">
    <path d="M0 0 L40 0 L20 36 Z" fill="#f08c00"/>
  </g>
  <rect x="16" y="196" width="224" height="36" fill="url(#grad)"/>
  <text x="20" y="188" font-size="16" fill="#333333">Hello</text>
</svg>
```

文字应同时出现在 Imported 与 SvgDom 两侧。人工核对：红矩形、蓝圆、绿描边椭圆、旋转橙三角、底部渐变条、以及底部「Hello」。

- [ ] **Step 2: 写会失败的渲染测试**

`adapter/tgfx/tests/SvgImportRenderTest.mm`：

```objc
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "MotionStudio/import/svg/SvgImporter.h"
#include "MotionStudio/model/Composition.h"
#include "MotionStudio/model/Document.h"
#include "MotionStudio/render/CommandBuilder.h"
#include "MotionStudio/render/SceneEvaluator.h"
#include "MotionStudio/undo/UndoManager.h"

#include "TgfxRenderAdapter.h"
#include "TgfxTestGPUEnvironment.h"

#include <tgfx/core/Data.h>
#include <tgfx/core/ImageInfo.h>
#include <tgfx/core/Stream.h>
#include <tgfx/svg/SVGDOM.h>

using motion::BuildCommands;
using motion::Color;
using motion::Composition;
using motion::Document;
using motion::PlayCommands;
using motion::SceneEvaluator;
using motion::TgfxRenderAdapter;
using motion::UndoManager;
using motion::svg::ImportSvgFileInto;
using tgfx_test::OutputPath;
using tgfx_test::SaveWebp;
using tgfx_test::TgfxTestGPUEnvironment;

namespace {

std::string FixturePath() {
    return (std::filesystem::path(__FILE__).parent_path() / ".." / ".." / ".." /
            "tests" / "import" / "svg" / "fixtures" / "kitchen_sink.svg")
        .lexically_normal()
        .string();
}

std::string ReadFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool HasNonBackgroundPixel(const std::vector<uint8_t> &pixels, uint8_t bgR, uint8_t bgG,
                           uint8_t bgB) {
    const size_t count = pixels.size() / 4;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t r = pixels[i * 4];
        const uint8_t g = pixels[i * 4 + 1];
        const uint8_t b = pixels[i * 4 + 2];
        if (r != bgR || g != bgG || b != bgB) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(SvgImportRenderTest, RendersKitchenSinkToWebp) {
    constexpr int kSize = 256;
    auto adapter = TgfxRenderAdapter::Make(kSize, kSize);
    if (!adapter) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }

    Document document;
    auto composition = std::make_unique<Composition>();
    composition->width = kSize;
    composition->height = kSize;
    composition->duration = 1;
    composition->backgroundColor = Color{0.95f, 0.95f, 0.95f, 1.f};
    auto *host = document.addComposition(std::move(composition));
    ASSERT_NE(host, nullptr);

    UndoManager undo;
    const auto imported = ImportSvgFileInto(document, undo, host->id, FixturePath());
    ASSERT_TRUE(imported.hasValue()) << imported.error();

    const auto state = SceneEvaluator::Evaluate(document, host->id, 0);
    ASSERT_TRUE(state.hasValue());
    adapter->beginFrame(kSize, kSize, state.value().backgroundColor, state.value().cornerRadius);
    PlayCommands(BuildCommands(state.value()), *adapter);
    adapter->endFrame();

    std::vector<uint8_t> pixels;
    ASSERT_TRUE(adapter->ReadPixels(pixels));
    const std::string importedPath = OutputPath("SvgImport_KitchenSink_Imported.webp");
    ASSERT_TRUE(SaveWebp(pixels, kSize, kSize, importedPath)) << "failed to save " << importedPath;
    EXPECT_TRUE(HasNonBackgroundPixel(pixels, 242, 242, 242))
        << "imported frame looks empty; inspect " << importedPath;

    auto env = TgfxTestGPUEnvironment::Make(kSize, kSize);
    if (!env) {
        GTEST_SKIP() << "Metal is unavailable on this machine";
    }
    auto *context = env->lockContext();
    ASSERT_NE(context, nullptr);
    const std::string svgBytes = ReadFile(FixturePath());
    auto stream = tgfx::Stream::MakeFromData(
        tgfx::Data::MakeWithCopy(svgBytes.data(), svgBytes.size()));
    ASSERT_NE(stream, nullptr);
    auto dom = tgfx::SVGDOM::Make(*stream);
    ASSERT_NE(dom, nullptr);
    auto *canvas = env->surface()->getCanvas();
    canvas->clear(tgfx::Color::FromRGBA(242, 242, 242, 255));
    dom->setContainerSize(tgfx::Size::Make(static_cast<float>(kSize), static_cast<float>(kSize)));
    dom->render(canvas);

    tgfx::ImageInfo info =
        tgfx::ImageInfo::Make(kSize, kSize, tgfx::ColorType::RGBA_8888, tgfx::AlphaType::Premultiplied);
    std::vector<uint8_t> refPixels(static_cast<size_t>(info.rowBytes() * info.height()));
    ASSERT_TRUE(env->surface()->readPixels(info, refPixels.data()));
    const std::string refPath = OutputPath("SvgImport_KitchenSink_SvgDom.webp");
    ASSERT_TRUE(SaveWebp(refPixels, kSize, kSize, refPath)) << "failed to save " << refPath;
    env->unlockContext();
}
```

`tgfx::Stream::MakeFromData` / `Data::MakeWithCopy` 以仓库当前 tgfx 头为准；若签名不同，实现时改成 `MakeFromFile(FixturePath())`（优先，少一次拷贝）。

- [ ] **Step 3: 跑测试确认失败**

```bash
cmake --build build --target tgfx_adapter_test
./build/adapter/tgfx/tgfx_adapter_test --gtest_filter='SvgImportRenderTest.*'
```

Expected: 链接失败（尚未链 `svg_import`）或 `ImportSvgFileInto` 未实现 → FAIL。尚无 WebP。

- [ ] **Step 4: CMake 接入并确认落盘**

`src/import/svg/CMakeLists.txt` 在 `svg_import` target 定义之后加：

```cmake
if(TARGET tgfx_adapter_test)
  target_link_libraries(tgfx_adapter_test PRIVATE svg_import)
endif()
```

不要改 `adapter/tgfx/CMakeLists.txt` 的 `add_files_by_extension`：该目录下新 `.mm` 会自动编进 `tgfx_adapter_test`。

跑同一条 gtest。Expected：PASS；打开

```
adapter/tgfx/tests/out/SvgImport_KitchenSink_Imported.webp
adapter/tgfx/tests/out/SvgImport_KitchenSink_SvgDom.webp
```

人工核对：红矩形、蓝圆、绿描边椭圆、旋转橙三角、底部橙→蓝渐变条、以及「Hello」位置与颜色大致一致。字体替换造成的字宽差可接受。差异记到实现备注，不为此改 Core。

- [ ] **Step 5: Commit**

```bash
git add tests/import/svg/fixtures/kitchen_sink.svg adapter/tgfx/tests/SvgImportRenderTest.mm src/import/svg/CMakeLists.txt
git commit -m "Add SVG import render dumps as WebP for visual review."
```

---

## Self-Review

**Spec coverage**

| Spec | Task |
|---|---|
| SVGDOM 解析、失败路径、source 尺寸 | 1 |
| VectorNetwork / 不走 asPath | 2 |
| 基本形状、中心锚点 | 3 |
| Group / transform / viewBox / opacity | 4 |
| 插入当前合成 + CompositeCommand undo | 5 |
| 继承、dash、display、visibility | 6 |
| 渐变、use | 7 |
| image、clip、mask/filter diagnostic、architecture.md | 8 |
| `<text>` 点文本、tspan 拼接、textPath skip | 9 |
| 导入后 evaluate→play 落 WebP（人工核对，非像素门禁） | 10 |
| 不改 Core/schema/bridge/App | 全程 |
| `TGFX_BUILD_SVG=ON` | 1 |

**未列入实现（spec 非目标）：** Bridge/App UI、`<textPath>`、逐字定位、嵌入字体、SMIL、dash 实装、pattern、滤镜求值。

**类型一致性：** `BuildSvgLayers` / `ImportSvgInto` / `SvgLayerTree` / `ImportOptions` 与 spec §3 同名同字段。
