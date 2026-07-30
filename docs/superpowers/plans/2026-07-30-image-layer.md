# Image Layer 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** 端到端图片图层：包内 `assets/` 资源、空占位层 + Inspector 绑定、容器 size 与 transform 分离、PAG 对齐的 scaleMode、画布绘制与存盘重开。

**Architecture:** 扩展 `Asset` / `ImageContent` → `SceneEvaluator` 产出 `EvaluatedImageItem` → `DrawCommand::DrawImage`（自含绝对路径）→ tgfx 解码 LRU 绘制。App 负责 Project 导入与 Inspector 绑定；拖角按「容器 | 缩放」模式切换写 `image.size` 或 `transform.scale`。

**Tech Stack:** C++17 core、GoogleTest、tgfx Metal 适配器、C bridge、SwiftUI/UIKit App（Catalyst + iPad）。

**Spec:** `docs/superpowers/specs/2026-07-30-image-layer-design.md`

**Progress (2026-07-30):** Tasks 1–8 complete and committed on this branch (model → serialize → property path → layout → evaluate → DrawImage → tgfx → bridge). Tasks 9–12 App UI / docs implemented in working tree; pending review commit.

## Global Constraints

- 不升 `schemaVersion`，不做旧文档迁移；直接改当前 JSON。
- Core 不依赖 tgfx；图片经 `DrawCommand` / `RenderAdapter::drawImage` 表达。
- `Asset.path` 只存相对路径（如 `assets/photo.png`）；绝对路径仅在求值/命令中由 `projectRoot` 拼出。
- 导入与建层分离：工具栏只建空层；导入仅在 Project 面板；Inspector 绑定 asset + scaleMode。
- undo **不**删除磁盘 asset 文件。
- 拖角模式用**明确 UI 切换**，不用修饰键快捷方式。
- Core / 适配器 / 测试 / 文档：任务完成后可自动提交（不推送）。
- Bridge + App UI：实现后可先本地验证，**含交互的 UI 人工确认后再提交**（或按用户要求提交）。

## File Map

| 区域 | 文件 |
|---|---|
| 模型 | `include/MotionStudio/model/Asset.h`、`ImageContent.h`、新建 `ImageScaleMode.h`、`Document.h` |
| PropertyPath | `src/model/PropertyPath.cpp`、`include/MotionStudio/model/PropertyPath.h` |
| 序列化 | `src/serialization/Serializer.cpp`、`Dto.cpp` / `Dto.h` |
| 布局数学 | 新建 `include/MotionStudio/render/ImageScaleLayout.h`、`src/render/ImageScaleLayout.cpp` |
| 求值 | `EvaluatedLayer.h`、新建 `EvaluatedImageItem.h`、`SceneEvaluator.cpp`、`HitTest.cpp` |
| 指令 | `DrawCommand.h`、`CommandBuilder.cpp`、`RenderAdapter.h/.cpp` |
| 适配器 | `adapter/tgfx/`（`TgfxCanvasAdapter` + 图片缓存） |
| 桥接 | `bridge/include/motionstudio_bridge.h`、`motionstudio_bridge_document.cpp`、commands/layer |
| App | `MotionProjectDocument.swift`、`ProjectPanelView.swift`、`MotionDocumentCore.swift`、Inspector、`FreeTransformDrag.swift`、Editor Commands |
| 测试 | `tests/model/`、`tests/serialization/`、`tests/render/`、`bridge/tests/`、`adapter/tgfx/tests/`、App tests |
| 文档 | `docs/data-model.md`、本 plan / spec |

---

### Task 1: 模型 — ImageScaleMode / Asset 宽高 / ImageContent

**Files:**
- Create: `include/MotionStudio/model/ImageScaleMode.h`
- Modify: `include/MotionStudio/model/Asset.h`
- Modify: `include/MotionStudio/model/ImageContent.h`、`src/model/ImageContent.cpp`
- Modify: `include/MotionStudio/model/Document.h`（`projectRoot`）
- Test: `tests/model/ImageContentTest.cpp`（新建）或扩现有 model 测试
- CMake：确保新 `.cpp` 若有则加入；本任务头文件为主

**Interfaces:**
- Produces:
  - `enum class ImageScaleMode : uint8_t { None=0, Stretch=1, LetterBox=2, Zoom=3 };`
  - `Asset::{width, height}`（默认 0）
  - `ImageContent::{assetId, Animatable<Vec2> size{Vec2{200,200}}, ImageScaleMode scaleMode = LetterBox}`
  - `Document::projectRoot`：`std::string`，**不序列化**

- [x] **Step 1: 写失败测试**

```cpp
TEST(ImageContentTest, DefaultsMatchSpec) {
    ImageContent content;
    EXPECT_FALSE(content.assetId.isValid());
    EXPECT_FALSE(content.size.isAnimated());
    EXPECT_EQ(content.size.staticValue().x, 200.0f);
    EXPECT_EQ(content.size.staticValue().y, 200.0f);
    EXPECT_EQ(content.scaleMode, ImageScaleMode::LetterBox);
}

TEST(AssetTest, DefaultSizeZero) {
    Asset asset;
    EXPECT_EQ(asset.width, 0);
    EXPECT_EQ(asset.height, 0);
}
```

- [x] **Step 2: 跑测试确认失败**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='ImageContentTest.*:AssetTest.DefaultSizeZero'
```

Expected: 编译失败或断言失败（字段不存在）。

- [x] **Step 3: 最小实现**

`ImageScaleMode.h`：

```cpp
#pragma once
#include <cstdint>
namespace motion {
enum class ImageScaleMode : uint8_t {
    None = 0,
    Stretch = 1,
    LetterBox = 2,
    Zoom = 3,
};
}
```

`Asset` 增加 `int width = 0; int height = 0;`  
`ImageContent` 增加 `Animatable<Vec2> size{Vec2{200, 200}}; ImageScaleMode scaleMode = ImageScaleMode::LetterBox;`  
`Document` 增加 `std::string projectRoot;`（注释标明非持久化）。

- [x] **Step 4: 测试通过后提交**

```bash
./build/tests/core_tests --gtest_filter='ImageContentTest.*:AssetTest.DefaultSizeZero'
git commit --only <相关文件> -m "Add image layer model fields for container size and scale mode."
```

---

### Task 2: 序列化 Asset 宽高 + ImageContent size/scaleMode

**Files:**
- Modify: `src/serialization/Dto.cpp` / `Dto.h`（scaleMode 字符串互转）
- Modify: `src/serialization/Serializer.cpp`（`AssetToJson`/`AssetFromJson`、`ContentToJson`/`ContentFromJson` Image 分支）
- Test: `tests/serialization/SerializerTest.cpp`（已有 Image 用例，扩展）

**Interfaces:**
- Consumes: Task 1 字段
- Produces: JSON
  - Asset: `width` / `height`（int）
  - Image content: `size`（Animatable Vec2）、`scaleMode`（`"none"|"stretch"|"letterBox"|"zoom"`，默认 letterBox）

- [x] **Step 1: 写失败 round-trip 测试**

扩展现有 Image 序列化测试：asset 带 width/height；ImageContent 带 animated 或静态 size + scaleMode Zoom；deserialize 后字段一致。

- [x] **Step 2: 实现 DTO + Serializer**

`AssetToJson` 增加 width/height；`AssetFromJson` 读取（缺省 0，兼容旧 JSON）。  
Image 分支写/读 `size`、`scaleMode`。缺 `scaleMode` → LetterBox；缺 `size` → `200,200`。

- [x] **Step 3: 跑测试**

```bash
ctest --test-dir build -R SerializerTest --output-on-failure
```

- [x] **Step 4: 提交**

```bash
git commit --only <相关文件> -m "Serialize image asset dimensions container size and scale mode."
```

---

### Task 3: PropertyPath `image.size` + 文档注释

**Files:**
- Modify: `include/MotionStudio/model/PropertyPath.h`（注释示例）
- Modify: `src/model/PropertyPath.cpp`
- Test: 现有 PropertyPath 测试文件（或新建 `tests/model/PropertyPathImageTest.cpp`）

**Interfaces:**
- Produces: `ResolveAnimatable(doc, {layerId, "image.size"})` → `&ImageContent::size`
- 说明：路径为 `image.size`（两段），**不是**裸 `size`（避免与 Shape 几何 size 冲突）

- [x] **Step 1: 失败测试**

```cpp
TEST(PropertyPathImageTest, ResolvesImageSize) {
    Document doc;
    auto layer = std::make_unique<Layer>(LayerType::Image);
    EntityId id = layer->id;
    doc.addLayer(doc.compositions[0]->id, std::move(layer)); // 按项目现有 add 方式
    AnimatableBase *anim = ResolveAnimatable(doc, PropertyPath{id, "image.size"});
    ASSERT_NE(anim, nullptr);
}
```

（按仓库现有 Document/Composition 构造方式改写。）

- [x] **Step 2: 在 ResolveAnimatable 的 Layer 分支增加**

```cpp
if (first.name == "image" && segments.size() == 2 &&
    layer->content->type() == LayerType::Image) {
    auto *image = static_cast<ImageContent *>(layer->content.get());
    if (segments[1].name == "size") {
        return &image->size;
    }
    return nullptr;
}
```

- [x] **Step 3: 测试通过；确认 `SetStaticValueCommand` 可写 Vec2**
- [x] **Step 4: 提交**

```bash
git commit --only <相关文件> -m "Resolve image.size property path for image layer containers."
```

---

### Task 4: ImageScaleLayout 纯函数

**Files:**
- Create: `include/MotionStudio/render/ImageScaleLayout.h`
- Create: `src/render/ImageScaleLayout.cpp`
- Test: `tests/render/ImageScaleLayoutTest.cpp`
- CMake: 把新源文件加入 `core` / `core_tests`

**Interfaces:**
- Produces:

```cpp
struct Rect2 { float x, y, width, height; }; // 或复用项目已有矩形类型

// 在容器 [0,0,cw,ch] 内，按 mode 与 intrinsic 计算图片 dstRect（layer 局部）
Rect2 ComputeImageDestinationRect(Vec2 containerSize, Vec2 intrinsicSize, ImageScaleMode mode);
```

语义（与 spec 一致）：
- Stretch：dst = 整个容器
- None：dst = (0,0,iw,ih)（调用方负责 clip）
- LetterBox：等比完整放入，居中
- Zoom：等比盖满，居中
- container 或 intrinsic ≤ 0：返回空矩形（width/height ≤ 0）

- [x] **Step 1: 写失败测试（至少 4 个 mode + 边界）**

```cpp
TEST(ImageScaleLayoutTest, LetterBoxCentersInside) {
    auto r = ComputeImageDestinationRect({200, 100}, {100, 100}, ImageScaleMode::LetterBox);
    EXPECT_FLOAT_EQ(r.width, 100.f);
    EXPECT_FLOAT_EQ(r.height, 100.f);
    EXPECT_FLOAT_EQ(r.x, 50.f);
    EXPECT_FLOAT_EQ(r.y, 0.f);
}

TEST(ImageScaleLayoutTest, StretchFills) {
    auto r = ComputeImageDestinationRect({200, 100}, {50, 50}, ImageScaleMode::Stretch);
    EXPECT_FLOAT_EQ(r.x, 0.f);
    EXPECT_FLOAT_EQ(r.y, 0.f);
    EXPECT_FLOAT_EQ(r.width, 200.f);
    EXPECT_FLOAT_EQ(r.height, 100.f);
}
```

再补 Zoom（宽容器矮图应裁左右或上下）、None。

- [x] **Step 2: 实现并跑通**

```bash
./build/tests/core_tests --gtest_filter='ImageScaleLayoutTest.*'
```

- [x] **Step 3: 提交**

```bash
git commit --only <相关文件> -m "Add ImageScaleLayout helpers matching PAG scale modes."
```

---

### Task 5: SceneEvaluator + Hit/Bounds

**Files:**
- Create: `include/MotionStudio/render/EvaluatedImageItem.h`
- Modify: `include/MotionStudio/render/EvaluatedLayer.h`
- Modify: `src/render/SceneEvaluator.cpp`
- Modify: `src/render/HitTest.cpp`
- Test: `tests/render/SceneEvaluatorTest.cpp` 或新建 `ImageLayerEvalTest.cpp`

**Interfaces:**
- Produces: `EvaluatedLayer::imageItem`（`std::optional<EvaluatedImageItem>`）
- `EvaluatedImageItem` 字段见 spec
- 绝对路径：`projectRoot` 非空且 asset 有效时拼接；用平台无关方式（`projectRoot + "/" + path`，注意已有分隔符）
- Hit/Bounds：若有 `imageItem`（即使 path 空），用容器 `[0,0]–[cw,ch]`；`cw/ch ≤ 0` 则失败

- [x] **Step 1: 失败测试**

1. Image 层绑定 asset、设 projectRoot → evaluated 含 absolutePath、containerSize、intrinsicSize、scaleMode  
2. 未绑定 asset → `imageItem` 仍有（容器 bounds），`absolutePath` 空  
3. `BoundsOfLayerLocal` 返回容器尺寸  
4. `HitTestLayer` 点在容器内命中

- [x] **Step 2: 实现 EvaluateLayer 的 Image 分支**（与 Shape 分支并列；Image 不填 shapeItems）
- [x] **Step 3: 改 HitTest / BoundsOfLayer / BoundsOfLayerLocal**
- [x] **Step 4: 测试通过并提交**

```bash
git commit --only <相关文件> -m "Evaluate image layers into scene state with container bounds."
```

---

### Task 6: DrawCommand::DrawImage + CommandBuilder + PlayCommands

**Files:**
- Modify: `include/MotionStudio/render/DrawCommand.h`
- Modify: `src/render/CommandBuilder.cpp`
- Modify: `include/MotionStudio/render/RenderAdapter.h`
- Modify: `src/render/RenderAdapter.cpp`
- Test: `tests/render/CommandBuilderTest.cpp`（或新建）

**Interfaces:**
- Produces:
  - `DrawCommandType::DrawImage`
  - 字段：`imagePath`、`imageContainerSize`、`imageIntrinsicSize`、`imageScaleMode`
  - `RenderAdapter::drawImage(const std::string &path, Vec2 container, Vec2 intrinsic, ImageScaleMode mode) = 0;`
  - `PlayCommands` 分发到 `drawImage`
  - `BuildCommands`：`imageItem` 且 `!absolutePath.empty()` 时追加 DrawImage（仍包在 Save/Transform/Opacity/Blend/masks 中）

- [x] **Step 1: 失败测试** — Image 层 + 有效 path → 命令列表含 DrawImage，字段正确；path 空 → 无 DrawImage但仍有 Save/Transform
- [x] **Step 2: 改 DrawCommand / CommandBuilder / RenderAdapter / PlayCommands**
- [x] **Step 3: 所有实现 `RenderAdapter` 的类加 `drawImage` 默认空实现或纯虚实现**（编译全过）
- [x] **Step 4: 测试通过并提交**

```bash
git commit --only <相关文件> -m "Emit DrawImage commands for evaluated image layers."
```

---

### Task 7: tgfx drawImage + LRU 缓存 + 快照

**Files:**
- Modify: `adapter/tgfx/` 中 `TgfxCanvasAdapter` 相关头/源（按现有模块拆分放入合适 `.cpp`）
- 可能新建：`TgfxImageCache.h/.cpp`
- Test: `adapter/tgfx/tests/TgfxRenderAdapterTest.cpp`（或新建 Image 快照用例）
- Fixture: 小 PNG 放 `adapter/tgfx/tests/fixtures/` 或 `tests/` 下已有资源目录

**Interfaces:**
- Consumes: `drawImage(path, container, intrinsic, mode)`
- 行为：LRU 按 path 缓存 `tgfx::Image`；clip 容器；`ComputeImageDestinationRect` 后 `drawImageRect`；失败 no-op

- [x] **Step 1: 失败快照测试** — LetterBox 与 Stretch 各一帧（固定容器/图尺寸）
- [x] **Step 2: 实现 drawImage + 缓存**
- [x] **Step 3: 跑 tgfx 测试**

```bash
ctest --test-dir build -R Tgfx --output-on-failure
```

- [x] **Step 4: 提交**

```bash
git commit --only <相关文件> -m "Draw image layers in tgfx with path based image cache."
```

---

### Task 8: Bridge — load 包路径 / project root / import / add / bind

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`
- Modify: `bridge/src/common/motionstudio_bridge_document.cpp`
- Modify: `bridge/src/common/motionstudio_bridge_commands.cpp`（或新建 image 相关 cpp）
- Modify: `bridge/src/common/motionstudio_bridge_layer.cpp`
- Modify: `bridge/src/common/MSDocument.h`（若需）
- Modify: 所有调用 `ms_document_load(json...)` 的测试 → `ms_document_load_json`
- Test: `bridge/tests/BridgeTest.cpp`（新增用例）
- Undo：新建 `ImportImageAssetCommand` / 或内联 Execute + 可 undo 的 assets 增删；`AddLayerCommand` 复用；`SetImageAssetCommand` / scaleMode 命令（可简单非合并命令）

**Interfaces（C API 最终形态）:**

```c
MSDocument *ms_document_load_json(const char *jsonText, size_t length, char **errorOut);
MSDocument *ms_document_load(const char *packagePath, char **errorOut);
void ms_document_set_project_root(MSDocument *doc, const char *absolutePath);
char *ms_document_project_root(MSDocument *doc);

uint64_t ms_command_import_image_asset(MSDocument *doc,
                                       const char *sourceAbsolutePath,
                                       const char *preferredFileName);
uint64_t ms_command_add_image_layer(MSDocument *doc, uint64_t compositionId);
bool ms_layer_set_image_asset(MSDocument *doc, uint64_t layerId, uint64_t assetId);
void ms_layer_set_image_scale_mode(MSDocument *doc, uint64_t layerId, int mode);
int ms_layer_image_scale_mode(MSDocument *doc, uint64_t layerId);
```

约定：
- `ms_document_load(packagePath)`：读 `{packagePath}/document.json`，`projectRoot = packagePath`
- `import`：要求已设 projectRoot；拷贝到 `{root}/assets/`（目录不存在则创建）；重名加后缀；ImageIO（Apple）或测试里预置宽高探测读 width/height；undo 只从 `document.assets` 移除，**不删文件**
- `add_image_layer`：空层，size 200×200，anchor (100,100)，position = 合成中心，scaleMode LetterBox
- `set_image_asset(0)` 解绑；绑定**不**改 size

- [x] **Step 1: 迁移现有 `ms_document_load` → `ms_document_load_json`，全部 bridge 测试改名调用**
- [x] **Step 2: 实现 `ms_document_load(packagePath)` + set/get project_root；加测试**
- [x] **Step 3: 实现 import / add_image_layer / set asset / scaleMode + undo 测试**
- [x] **Step 4: 跑 bridge_test**

```bash
ctest --test-dir build -R Bridge --output-on-failure
```

- [x] **Step 5: 提交**

```bash
git commit --only <相关文件> -m "Add bridge APIs for package load image import and image layers."
```

---

### Task 9: App — Core 封装 + 文档包 assets 真写入 + project root

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Document/MotionProjectDocument.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Document/MotionProjectState.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController+Saving.swift`（如需）
- Test: `apps/MotionStudioApp/MotionStudioAppTests/MotionStudioAppTests.swift`

**Interfaces:**
- `MotionDocumentCore`：`loadJson` / `openPackage(path:)` / `setProjectRoot` / `importImageAsset` / `addImageLayer` / `setImageAsset` / `imageScaleMode` / `setImageScaleMode` / `resetImageSizeToIntrinsic`（写 `image.size`）
- `packageFileWrapper`：遍历 `{saveURL}/assets` 或内存中已导入文件列表，把真实文件放进 `assets` FileWrapper（不再空目录）
- 打开文档：`fileURL` 可用时 `setProjectRoot`；优先包路径 load

- [ ] **Step 1: 封装 bridge 新 API；修复因 `ms_document_load` 签名变更导致的编译错误**
- [ ] **Step 2: package 写出真实 assets；测试断言包内存在导入文件**
- [ ] **Step 3: 打开/保存/Save As 时维护 projectRoot**
- [ ] **Step 4: 提交（若仅文档/模型封装可提交；UI 未接可先提交 Core）**

```bash
git commit --only <相关文件> -m "Wire package assets and project root into the app document model."
```

---

### Task 10: App UI — Project 导入 / Add 空层 / Inspector

**Files:**
- Modify: `ProjectPanelView.swift`（导入按钮 + asset 列表）
- 可能新建：`ImageImportCoordinator.swift`（PHPicker / UIDocumentPicker / iPad ActionSheet）
- Modify: `EditorViewController+Commands.swift`（`addImageLayer` 去 alert）
- Create: `ImageLayerInspector.swift`
- Modify: `InspectorView.swift`（按层类型挂载）
- Modify: `MotionDocumentCore` 列表 assets 的查询 API（若尚无）

**行为:**
- Project：Import → Catalyst 文件选择；iPad ActionSheet「相册 / 文件」→ 拷贝+import command → 刷新列表
- 工具栏 Add Image → `addImageLayer` → 选中新层
- Inspector（Image 层）：Asset 下拉（含「无」解绑）、scaleMode 四选一、按钮「重置为源尺寸」
- 时间轴/侧栏图标：若有 layer type 图标，Image 用 `photo` SF Symbol（顺手，非必须）

- [ ] **Step 1: Add Image 接空层**
- [ ] **Step 2: Project 导入 + 列表**
- [ ] **Step 3: ImageLayerInspector**
- [ ] **Step 4: 人工验证（Catalyst + 模拟器）后提交**

```bash
git commit --only <相关文件> -m "Add project image import empty image layers and inspector binding."
```

---

### Task 11: 拖角模式 — 容器 | 缩放

**Files:**
- Modify: `FreeTransformDrag.swift`
- Modify: `CanvasViewController.swift`（或工具条所在文件）— 单选 Image 时显示分段控件
- Modify: `EditorState.swift` — `imageResizeMode: container | transform`（默认 container）
- Test: 若有 FreeTransform 单测则扩展；否则手动验证清单写入 PR 描述

**行为:**
- 仅**单选**且层类型为 Image 时显示切换；默认「容器」
- 容器模式：角/边拖改写 `image.size`（PropertyPath），按对侧锚点调整 `position`（复用现有 pivot 数学，把 scale 因子换成 size 增量）
- 缩放模式：保持现有 `transform.scale` 逻辑
- 多选 / 非 Image：隐藏切换，始终 transform

- [ ] **Step 1: EditorState + UI 分段控件**
- [ ] **Step 2: FreeTransformDrag 容器分支**
- [ ] **Step 3: 人工验证两种模式后提交**

```bash
git commit --only <相关文件> -m "Add container versus transform resize mode for image layers."
```

---

### Task 12: 文档同步

**Files:**
- Modify: `docs/data-model.md`（Asset / ImageContent / ImageScaleMode / projectRoot 说明）
- 如有 `docs/rendering.md` 提及 DrawCommand，补 `DrawImage` 一行

- [ ] **Step 1: 按实现更新文档，去掉过时「仅 assetId」描述**
- [ ] **Step 2: 提交**

```bash
git commit --only docs/data-model.md docs/rendering.md -m "Document image layer assets container size and draw image commands."
```

---

## Spec 覆盖自检

| Spec 要求 | Task |
|---|---|
| ImageScaleMode 四态默认 LetterBox | 1, 2, 4, 10 |
| Asset 相对路径 + width/height | 1, 2, 8 |
| ImageContent size Animatable + scaleMode | 1–3 |
| projectRoot 非持久化；load 包路径 | 1, 8, 9 |
| 导入 / 建空层 / Inspector 绑定分离 | 8, 10 |
| 绑定不改 size；重置源尺寸 | 8, 10 |
| EvaluatedImageItem + DrawImage 自含路径 | 5, 6 |
| Hit/Bounds 用容器 | 5 |
| tgfx 缓存绘制 | 7 |
| undo 不删文件 | 8 |
| 拖角模式 UI 无快捷键 | 11 |
| package 写入真实 assets | 9 |
| iPad 相册或文件 / Catalyst 文件 | 10 |

## 执行方式

计划已保存。可选：

1. **Subagent-Driven（推荐）** — 每任务新子代理，任务间复习  
2. **Inline Execution** — 本会话按 executing-plans 连续执行  

选哪种？
