# Text Layer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 端到端文本图层：虚拟容器框排版（换行 / `autoHeight` / 固定高缩字）、`Layer.styles` 填充描边、Font Asset + PingFang SC 回退、画布绘制与 Inspector 编辑。

**Architecture:** Core 只求值原始 `EvaluatedTextItem` → `DrawCommand::DrawText`。独立 `adapter/textlayout` 做换行/对齐/缩字（`GlyphMetrics` 抽象）；tgfx adapter 提供 metrics 与绘制。拖改 `content.size` 时等比更新 `anchorPoint`、保持 `position`。

**Tech Stack:** C++17 core、GoogleTest、`adapter/textlayout`、tgfx Metal、C bridge、Swift App。

**Spec:** `docs/superpowers/specs/2026-07-30-text-layer-design.md`

**Progress:** Tasks 1–2 complete.

## Global Constraints

- 不升 `schemaVersion`；直接改当前 JSON。
- Core **不**依赖 tgfx；排版在 `adapter/textlayout`，绘制在 tgfx adapter。
- 默认 `fontFamily = "PingFang SC"`；解析顺序：Font Asset 路径 → `fontFamily` → PingFang SC → Helvetica。
- 填充/描边只读 `Layer.styles` 首个 Fill / Stroke；不做垂直对齐、字距行距、富文本、画布原地编辑、导出。
- 拖改 size：`anchor' = (ax*w1/w0, ay*h1/h0)`，`position` 不变；同一 undo merge group。
- `autoHeight` 测高**不**回写模型 `size`/`anchor`。
- Core / textlayout / 适配器 / 测试：任务完成后可自动提交（不推送）。
- Bridge + App UI：可先本地验证；含交互的 UI 人工确认后再提交（或按用户要求提交）。

## File Map

| 区域 | 文件 |
|---|---|
| 模型 | `include/MotionStudio/model/TextContent.h`、新建 `TextAlign.h`、`PropertyPath.cpp` |
| Undo | 新建 `SetTextAutoHeightCommand` / `SetTextAlignCommand` / `SetTextFontFamilyCommand` / `SetTextFontAssetCommand` / `ImportFontAssetCommand`；可选复用 Image 的 import 模式 |
| 序列化 | `Serializer.cpp`、`Dto.cpp` / `Dto.h` |
| 求值/指令 | `EvaluatedTextItem.h`、`EvaluatedLayer.h`、`SceneEvaluator.cpp`、`DrawCommand.h`、`CommandBuilder.cpp`、`RenderAdapter.*`、`HitTest.cpp`、`SelectionHandles`（若按容器矩形） |
| 锚点辅助 | 新建 `include/MotionStudio/common/ScaleAnchor.h`（或放 `render/`） |
| 排版 | 新建 `adapter/textlayout/`（`GlyphMetrics.h`、`TextLayout.h/.cpp`、CMake、tests） |
| tgfx | `TgfxCanvasAdapter.*`、`TgfxGlyphMetrics.*`、`adapter/tgfx/CMakeLists.txt` |
| 桥接 | `motionstudio_bridge.h`、新建 `motionstudio_bridge_text.cpp`、property string API、canvas 测高挂钩 |
| App | `MotionDocumentCore`、`TextLayerInspector`、`InspectorView`、Commands/Layout、`FreeTransformDrag` / `CanvasViewController`、Project Font 导入 |
| 文档 | `docs/data-model.md`、`docs/rendering.md` |

---

### Task 1: 模型 — TextAlign / TextContent 字段

**Files:**
- Create: `include/MotionStudio/model/TextAlign.h`
- Modify: `include/MotionStudio/model/TextContent.h`
- Test: `tests/model/TextContentTest.cpp`（新建）

**Interfaces:**
- Produces:
  - `enum class TextAlign : uint8_t { Left=0, Center=1, Right=2 };`
  - `TextContent::{text{"Text"}, fontAssetId, fontFamily{"PingFang SC"}, fontSize{48}, size{400,120}, autoHeight{true}, align{Left}}`

- [x] **Step 1: 写失败测试**

```cpp
TEST(TextContentTest, DefaultsMatchSpec) {
    TextContent content;
    EXPECT_EQ(content.text.staticValue(), "Text");
    EXPECT_FALSE(content.fontAssetId.isValid());
    EXPECT_EQ(content.fontFamily, "PingFang SC");
    EXPECT_FLOAT_EQ(content.fontSize.staticValue(), 48.0f);
    EXPECT_FLOAT_EQ(content.size.staticValue().x, 400.0f);
    EXPECT_FLOAT_EQ(content.size.staticValue().y, 120.0f);
    EXPECT_TRUE(content.autoHeight);
    EXPECT_EQ(content.align, TextAlign::Left);
}
```

- [x] **Step 2: 跑测确认失败**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='TextContentTest.*'
```

Expected: 编译失败或断言失败。

- [x] **Step 3: 最小实现** — 按 Interfaces 改头文件（`TextContent.cpp` 若无需改则不动）。

- [x] **Step 4: 测试通过并提交**

```bash
./build/tests/core_tests --gtest_filter='TextContentTest.*'
git commit --only <相关文件> -m "Extend TextContent with box size align and font asset fields."
```

---

### Task 2: 等比锚点辅助 ScaleAnchorForSizeChange

**Files:**
- Create: `include/MotionStudio/common/ScaleAnchor.h`（header-only 即可）
- Test: `tests/common/ScaleAnchorTest.cpp`（新建；加入 `tests/CMakeLists.txt`）

**Interfaces:**
- Produces:

```cpp
namespace motion {
inline Vec2 ScaleAnchorForSizeChange(Vec2 oldSize, Vec2 newSize, Vec2 oldAnchor) {
    float x = oldSize.x != 0.0f ? oldAnchor.x * (newSize.x / oldSize.x) : oldAnchor.x;
    float y = oldSize.y != 0.0f ? oldAnchor.y * (newSize.y / oldSize.y) : oldAnchor.y;
    return Vec2{x, y};
}
}
```

- [ ] **Step 1: 测试**

```cpp
TEST(ScaleAnchorTest, ScalesProportionally) {
    EXPECT_EQ(ScaleAnchorForSizeChange({400, 120}, {800, 240}, {200, 60}), (Vec2{400, 120}));
}
TEST(ScaleAnchorTest, ZeroOldWidthKeepsX) {
    EXPECT_EQ(ScaleAnchorForSizeChange({0, 100}, {50, 200}, {10, 50}), (Vec2{10, 100}));
}
```

- [ ] **Step 2: 实现 + 通过 + 提交**

```bash
./build/tests/core_tests --gtest_filter='ScaleAnchorTest.*'
git commit --only <相关文件> -m "Add ScaleAnchorForSizeChange helper for box resize."
```

---

### Task 3: PropertyPath `content.size` + 序列化

**Files:**
- Modify: `src/model/PropertyPath.cpp`、`include/MotionStudio/model/PropertyPath.h`（注释示例）
- Modify: `src/serialization/Serializer.cpp`（Text 分支）
- Modify: `src/serialization/Dto.cpp`、`include/MotionStudio/serialization/Dto.h`（`TextAlign` ToString / FromString）
- Test: 扩展 `tests/serialization/SerializerTest.cpp`；`tests/model/PropertyPathTest.cpp`（若有）或新建断言

**Interfaces:**
- `content.size` → `&TextContent::size`
- JSON Text content 增：`fontAssetId`（hex 或省略无效）、`size`（Animatable）、`autoHeight`、`align`（`"left"|"center"|"right"`）
- 缺省字段反序列化：兼容旧 JSON（无新字段时用 §1 默认值）

- [ ] **Step 1: 失败测试** — round-trip 含 size/autoHeight/align/fontAssetId；Resolve `content.size` 非空

- [ ] **Step 2: 实现**

- [ ] **Step 3: 通过并提交**

```bash
./build/tests/core_tests --gtest_filter='SerializerTest.*:PropertyPath*'
git commit --only <相关文件> -m "Serialize text box fields and resolve content.size."
```

---

### Task 4: Undo 命令 — autoHeight / align / fontFamily / fontAsset + ImportFontAsset

**Files:**
- Create: `include/MotionStudio/undo/SetTextAutoHeightCommand.h` + `.cpp`（及 align / fontFamily / fontAsset）
- Create: `include/MotionStudio/undo/ImportFontAssetCommand.h` + `.cpp`（可对照 `ImportImageAssetCommand`，`AssetType::Font`，width/height=0）
- Modify: `CommandKind.h`、命令注册/工厂若有
- Modify: `src/CMakeLists.txt` / undo 源列表
- Test: `tests/undo/TextCommandsTest.cpp`（新建）

**Interfaces:**
- `SetTextAutoHeightCommand(EntityId layerId, bool value)`
- `SetTextAlignCommand(EntityId layerId, TextAlign value)`
- `SetTextFontFamilyCommand(EntityId layerId, std::string family)`
- `SetTextFontAssetCommand(EntityId layerId, EntityId assetId)` — 无效 id 解绑；绑定时可把 `fontFamily` 设为 asset.name（与 spec「可同步显示名」一致）
- `ImportFontAssetCommand`：向 `document.assets` 追加 Font asset（path 相对、已由调用方拷贝文件）

- [ ] **Step 1: 失败测试** — execute/undo 还原字段；绑/解绑 fontAssetId

- [ ] **Step 2: 实现**

- [ ] **Step 3: 通过并提交**

```bash
./build/tests/core_tests --gtest_filter='TextCommandsTest.*'
git commit --only <相关文件> -m "Add undo commands for text box font and import font asset."
```

---

### Task 5: TextLayout 模块（假 metrics 单测）

**Files:**
- Create: `adapter/textlayout/include/MotionStudio/textlayout/GlyphMetrics.h`
- Create: `adapter/textlayout/include/MotionStudio/textlayout/TextLayout.h`
- Create: `adapter/textlayout/src/TextLayout.cpp`
- Create: `adapter/textlayout/CMakeLists.txt`
- Create: `adapter/textlayout/tests/TextLayoutTest.cpp`
- Modify: 根 `CMakeLists.txt`：`add_subdirectory(adapter/textlayout)`（不必限 Apple；纯 C++）

**Interfaces:**

```cpp
namespace motion::textlayout {

struct FontMetrics {
    float ascent = 0;   // 通常 >0，基线之上
    float descent = 0;  // 通常 >0，基线之下
    float leading = 0;
};

class GlyphMetrics {
public:
    virtual ~GlyphMetrics() = default;
    virtual FontMetrics metrics(float fontSize) const = 0;
    virtual float advance(uint32_t unichar, float fontSize) const = 0;
};

enum class Align { Left, Center, Right };

struct TextLayoutInput {
    std::string text;
    float boxWidth = 0;
    std::optional<float> boxHeight; // nullopt = autoHeight
    float fontSize = 48;
    Align align = Align::Left;
    const GlyphMetrics *metrics = nullptr;
};

struct TextLine {
    std::string text;   // 该行 UTF-8 子串（或存 [start,end) 亦可，实现选一种并固定）
    float x = 0;
    float y = 0;        // 基线 y，相对容器顶边
    float width = 0;
};

struct TextLayoutResult {
    float appliedFontSize = 0;
    Vec2 measuredSize;
    std::vector<TextLine> lines;
};

TextLayoutResult LayoutText(const TextLayoutInput &input);
}
```

**行为（写进测试）：**
- `\n` 硬换行；软换行优先空白否则按 UTF-8 码点
- `boxHeight == nullopt`：不缩字；`measuredSize.y` = 内容高；`appliedFontSize = fontSize`
- `boxHeight` 有值：二分缩字（下限如 1.0f）直到 `measuredHeight <= boxHeight` 且行宽不超 `boxWidth`；`measuredSize = {boxWidth, *boxHeight}`
- 对齐改每行 `x`；垂直顶对齐；行距 = ascent+descent+leading

- [ ] **Step 1: FakeGlyphMetrics**（等宽 advance = fontSize * 0.5，ascent=0.8*size，descent=0.2*size，leading=0）

- [ ] **Step 2: 失败测试** — 单行；`a\nb` 两行；窄宽强制软换行；固定高缩字 `appliedFontSize < fontSize`；Center 行 `x > 0`

- [ ] **Step 3: 实现 LayoutText**

- [ ] **Step 4: CMake 目标 `textlayout` + `textlayout_tests`，ctest 注册**

```bash
cmake -B build -G Ninja -DMOTIONSTUDIO_ENABLE_ASAN=ON
cmake --build build --target textlayout_tests
./build/adapter/textlayout/tests/textlayout_tests  # 或 ctest -R TextLayout
```

- [ ] **Step 5: 提交**

```bash
git commit --only <相关文件> -m "Add textlayout module with wrap align and font shrink."
```

---

### Task 6: SceneEvaluator + EvaluatedTextItem + Hit/Bounds

**Files:**
- Create: `include/MotionStudio/render/EvaluatedTextItem.h`
- Modify: `EvaluatedLayer.h`、`SceneEvaluator.cpp`、`HitTest.cpp`（及 SelectionHandles 若用图层局部 bounds）
- Test: `tests/render/TextLayerEvalTest.cpp`（新建）

**Interfaces:**

```cpp
struct EvaluatedTextItem {
    std::string text;
    float fontSize = 48;
    Vec2 containerSize;
    bool autoHeight = true;
    TextAlign align = TextAlign::Left;
    EntityId fontAssetId;
    std::string fontFamily;
    std::string fontAbsolutePath;
    Color fillColor{0, 0, 0, 1};
    std::optional<Color> strokeColor;
    float strokeWidth = 0;
    // Core 初值 = containerSize；Bridge/Canvas 在 autoHeight 下用 TextLayout 覆盖 y
    Vec2 hitSize;
};
```

- 从 `Layer.styles` 取**第一个** Fill / Stroke（evaluate 色与 width）
- 无 Fill → 黑；无 Stroke 或 width≤0 → 不设 strokeColor、strokeWidth=0
- `fontAbsolutePath`：Font asset + 非空 `projectRoot` 时拼接（同 Image）
- Hit/Bounds：有 `textItem` 时用局部矩形 `[0,0]–[hitSize.x, hitSize.y]`（`hitSize` 默认等于 `containerSize`）
- Text 层不填 `shapeItems` / `imageItem`

- [ ] **Step 1: 失败测试** — 默认层 evaluate 出 textItem；Fill 色进 fillColor；Stroke 进 stroke；bounds/hit 用容器

- [ ] **Step 2: 实现**

- [ ] **Step 3: 通过并提交**

```bash
./build/tests/core_tests --gtest_filter='TextLayerEvalTest.*'
git commit --only <相关文件> -m "Evaluate text layers into scene state with style paints."
```

---

### Task 7: DrawCommand::DrawText + CommandBuilder + PlayCommands

**Files:**
- Modify: `DrawCommand.h`、`CommandBuilder.cpp`、`RenderAdapter.h`、`RenderAdapter.cpp`
- 所有 `RenderAdapter` 子类加 `drawText`（可先空实现）
- Test: `tests/render/TextCommandBuilderTest.cpp`（新建）

**Interfaces:**
- `DrawCommandType::DrawText`
- 字段与 `EvaluatedTextItem` 对齐（除 `hitSize` / `fontAssetId` 可不进命令；命令需：`text`, `fontSize`, `containerSize`, `autoHeight`, `align`, `fontFamily`, `fontAbsolutePath`, `fillColor`, `strokeColor`, `strokeWidth`）
- `RenderAdapter::drawText(...)` 纯虚或带默认空实现
- `BuildCommands`：有 `textItem` 即追加 DrawText（空字符串仍可发，adapter no-op 字形）
- Track matte 源回放：`AppendTextItem` 与 Image 并列

- [ ] **Step 1: 失败测试** — evaluate+build 含 DrawText 字段

- [ ] **Step 2: 实现**

- [ ] **Step 3: 编译全绿 + 提交**

```bash
./build/tests/core_tests --gtest_filter='TextCommandBuilderTest.*'
git commit --only <相关文件> -m "Emit DrawText commands for evaluated text layers."
```

---

### Task 8: tgfx — TgfxGlyphMetrics + drawText + clip/缩字

**Files:**
- Create: `adapter/tgfx/src/TgfxGlyphMetrics.h/.cpp`（实现 `motion::textlayout::GlyphMetrics`）
- Modify: `TgfxCanvasAdapter.h/.cpp` — `drawText`
- Modify: `adapter/tgfx/CMakeLists.txt` — link `textlayout`
- Modify: 根 CMake 保证 `textlayout` 在 tgfx 前可被链接
- Test: `adapter/tgfx/tests/` 增加文本快照（若现有快照基建可复用；否则至少编译期调用 + 非空 surface 冒烟）

**Interfaces:**
- 字体：`MakeFromPath(fontAbsolutePath)` → else `MakeFromName(fontFamily)` → PingFang SC → Helvetica
- `LayoutText` → 按行 `TextBlob::MakeFrom` + `drawTextBlob`；有 stroke 时先 stroke 再 fill（或 tgfx Paint 支持的等价顺序）
- `autoHeight==false`：clip 到容器矩形
- 局部原点：容器左上为 (0,0)，与 Image 一致

- [ ] **Step 1: 实现 GlyphMetrics + drawText**

- [ ] **Step 2: 跑 tgfx 测试**

```bash
ctest --test-dir build -R Tgfx --output-on-failure
```

- [ ] **Step 3: 提交**

```bash
git commit --only <相关文件> -m "Draw text layers in tgfx using textlayout and typefaces."
```

---

### Task 9: Bridge — 加层 / 字体 / 字符串属性 / 测高挂钩

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`
- Create: `bridge/src/common/motionstudio_bridge_text.cpp`
- Modify: `motionstudio_bridge_property.cpp` — `ms_property_static_string` / `ms_command_set_static_string`（及 keyframe string 若 UI 需要 hold 关键帧；首版至少 static）
- Modify: canvas 路径：在 hit / selection / play 前对 `autoHeight` 文本调用 Layout，写回 `textItem.hitSize`（若 SceneState 在 bridge 内可变；否则提供 `ms_canvas_refresh_text_layout` 在 evaluate 后调用）
- Modify: bridge CMake 源列表；link `textlayout`（测高）+ 字体解析可复用 adapter 辅助或在 bridge 内用 tgfx Typeface 仅测宽——**优先**：测高只依赖 `textlayout` + 与 draw 相同的 `TgfxGlyphMetrics`（bridge 链 tgfx 已有则复用）
- Test: `bridge/tests/BridgeTest.cpp` 增补

**Interfaces（C API 示例）：**

```c
uint64_t ms_command_add_text_layer(MSDocument *document, uint64_t compositionId);
uint64_t ms_command_import_font_asset(MSDocument *document, const char *sourceAbsolutePath,
                                      const char *preferredFileName);
bool ms_command_set_text_font_asset(MSDocument *document, uint64_t layerId, uint64_t assetId);
bool ms_command_set_text_font_family(MSDocument *document, uint64_t layerId, const char *family);
bool ms_command_set_text_auto_height(MSDocument *document, uint64_t layerId, bool autoHeight);
bool ms_command_set_text_align(MSDocument *document, uint64_t layerId, int align /* MS_TEXT_ALIGN */);

char *ms_property_static_string(MSDocument *document, uint64_t entityId, const char *path); // ms_string_free
bool ms_command_set_static_string(MSDocument *document, uint64_t entityId, const char *path, const char *value);

// 查询
bool ms_layer_text_auto_height(...);
int ms_layer_text_align(...);
uint64_t ms_layer_text_font_asset(...);
char *ms_layer_text_font_family(...); // ms_string_free
```

**`add_text_layer` 行为：**
- `Layer(LayerType::Text)`，name 如 `"Text 1"`
- 默认字段见 spec；`position` = 合成中心；`anchor = (200,60)`
- `styles` 推入黑色 `FillStyle`
- `inPoint/outPoint` 对齐合成时长（同 add image/shape）

- [ ] **Step 1: Bridge 测试** — add → 查默认；set string/size；import font + bind；undo

- [ ] **Step 2: 实现**

- [ ] **Step 3: 提交**

```bash
ctest --test-dir build -R Bridge --output-on-failure
git commit --only <相关文件> -m "Expose text layer and font asset commands on the bridge."
```

---

### Task 10: App — Add Text / Inspector / 拖 size 同步锚点 / Font 导入

**Files:**
- Modify: `MotionDocumentCore.swift` — 包装 Task 9 API；`setTextBoxSize` 内同时 `ScaleAnchorForSizeChange`（或 Swift 侧算完写 size+anchor）
- Create: `TextLayerInspector.swift`
- Modify: `InspectorView.swift`、`InspectorProperty.swift`（`TextProperty`）
- Modify: `EditorViewController+Commands/Layout` — Add Text 按钮
- Modify: `CanvasViewController` / `FreeTransformDrag` — 文本层容器拖角写 `content.size` + 等比 anchor（可复用 Image 的 container 模式，但**始终**同步锚点；文本不必「缩放模式」切换，或与 Image 一样提供容器|缩放——spec 仅要求改 size 时同步锚点；**首版文本拖角只改容器 size+anchor**）
- Modify: `ProjectPanelView` / 新建 `FontImportCoordinator`（对照 `ImageImportCoordinator`，扩展名 ttf/otf/ttc）
- Modify: Timeline 若需展示 `content.fontSize` 轨道（有关键帧才显示即可）

**拖角逻辑（Swift）：**

```swift
let newSize = ...
let oldSize = start.containerSize
let newAnchor = CGPoint(
    x: oldSize.width != 0 ? start.anchor.x * newSize.width / oldSize.width : start.anchor.x,
    y: oldSize.height != 0 ? start.anchor.y * newSize.height / oldSize.height : start.anchor.y)
core.beginMergeGroup()
core.setStaticVec2(layerID, path: "content.size", value: newSize)
core.setStaticVec2(layerID, path: "transform.anchorPoint", value: newAnchor)
// position 不改
core.endMergeGroup() // 或 endDrag 时关
```

- [ ] **Step 1: Core facade + Add Text**

- [ ] **Step 2: TextLayerInspector**（文案 TextEditor、字号、W/H、autoHeight、对齐、字体名、Asset 绑定、Fill/Stroke 复用）

- [ ] **Step 3: 画布拖角 + Project Font 导入**

- [ ] **Step 4: 本地跑 App，人工确认验收清单**

- [ ] **Step 5: 确认后提交**

```bash
git commit --only <相关文件> -m "Add text layer UI with inspector font import and box resize."
```

---

### Task 11: 文档同步

**Files:**
- Modify: `docs/data-model.md` — 完整 `TextContent`、TextAlign、拖 size 锚点约定
- Modify: `docs/rendering.md` — `DrawText`、TextLayout 边界
- Modify: spec 状态可改为「实施中/已落地」若全部完成

- [ ] **Step 1: 按实现结果更新文档（勿写未实现行为）**

- [ ] **Step 2: 提交**

```bash
git commit --only docs/data-model.md docs/rendering.md docs/superpowers/specs/2026-07-30-text-layer-design.md \
  -m "Document text layer box model and DrawText pipeline."
```

---

## Spec 覆盖自检

| Spec 项 | Task |
|---|---|
| TextContent 字段 + PingFang 默认 | 1 |
| styles Fill/Stroke | 6, 10 |
| Font Asset + 回退 | 4, 8, 9, 10 |
| 换行 / autoHeight / 缩字 | 5, 8 |
| DrawText 管线 | 6, 7, 8 |
| 拖 size 等比锚点 | 2, 10 |
| Inspector / 无画布编辑 | 10 |
| 序列化 / undo | 3, 4 |
| 测试分层 | 各 Task |

## 类型名一致性

- 模型：`motion::TextAlign`；textlayout：`motion::textlayout::Align`（桥接时显式转换）
- Property：`content.text` / `content.fontSize` / `content.size`
- 命令字段前缀：`DrawText` 用 `text*` / 与 Image 的 `image*` 并列，实现时在 `DrawCommand` 增加成员勿复用 `imagePath`
