# Point Text vs PAG Box Text Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 默认点文本（仅 `\n`、无 clip、无 resize）；框文本 = PAG `boxText`（换行 + shrink）；`fontSize`/`size` 静态无关键帧；点→框时测当前字形设 `size`；PAG 导出按 `boxTextMode` 映射。

**Architecture:** Core 仍只求值原始 `EvaluatedTextItem`（不含排版）。`adapter/textlayout` 增加 `softWrap`；点文本 live bounds 与切模式测字在 bridge（链 tgfx）里用 `LayoutText` + `TgfxGlyphMetrics` 完成，并在 hit/handles/draw 求值后覆写 `textItem.containerSize`。专用 undo 命令改静态 `fontSize`/`size`；手柄与 Inspector 仅在 `boxTextMode` 下改 size。

**Tech Stack:** C++17 core、GoogleTest、`adapter/textlayout`、tgfx、C bridge、SwiftUI App、PAG export。

**Spec:** `docs/superpowers/specs/2026-08-01-point-vs-box-text-design.md`

## Global Constraints

- 不升 `schemaVersion`；`fontSize`/`size` 只写静态 JSON（`{"static": ...}`），不读旧 keyframes。
- Core **不**依赖 tgfx；字形测量只在 `adapter/textlayout` + bridge/tgfx。
- 点/框绘制均 **无** `clipRect`。
- 点文本：无角/边 resize 手柄；Width/Height 置灰；无 KF 按钮。
- 框文本：`shrinkToFit=true`；可拖/编 `size`；导出 `boxText=true`。
- 提交：每任务结束后 commit（不推送），除非用户另有指示。

## File Map

| 区域 | 文件 |
|---|---|
| 排版 | `adapter/textlayout/include/.../TextLayout.h`、`TextLayout.cpp`、`TextLayoutTest.cpp` |
| 模型 | `include/MotionStudio/model/TextContent.h`、`PropertyPath.cpp` / `.h` |
| Undo | `SetTextFontSizeCommand.*`、`SetTextSizeCommand.*`、扩展 `SetTextBoxTextModeCommand.*`、`CommandKind.h` |
| 序列化 | `src/serialization/Serializer.cpp`、相关测试 |
| 求值注释/文档 | `EvaluatedTextItem.h`、`docs/rendering.md`、`docs/data-model.md` |
| 绘制 | `adapter/tgfx/src/TgfxCanvasAdapter.cpp`、`RenderAdapter.h` |
| Bridge | `motionstudio_bridge.h`、`motionstudio_bridge_text.cpp`、`motionstudio_bridge_composition.cpp`、`motionstudio_bridge_canvas.cpp`（+ 测字 helper） |
| PAG | `PagFileBuilder.cpp`、`PagExporterTest.cpp`、`2026-07-31-pag-export-design.md` |
| App | `TextLayerInspector.swift`、`MotionDocumentCore.swift`、`FreeTransformDrag.swift`、`CanvasViewController.swift`、`TimelineSupport.swift` |

---

### Task 1: textlayout — `softWrap` + 点文本 `measuredSize`

**Status:** ✅ Done

**Files:**
- Modify: `adapter/textlayout/include/MotionStudio/textlayout/TextLayout.h`
- Modify: `adapter/textlayout/src/TextLayout.cpp`
- Modify: `adapter/textlayout/tests/TextLayoutTest.cpp`

**Interfaces:**
- Produces: `TextLayoutInput::softWrap`（默认 `true`）；`softWrap==false` 时不按宽断行，`measuredSize = {maxLineWidth, contentHeight}`，对齐参考宽 = `maxLineWidth`

- [x] **Step 1: 写失败测试**

```cpp
TEST(TextLayoutTest, PointTextNoSoftWrapMeasuresContent) {
    FakeGlyphMetrics metrics;
    TextLayoutInput input = MakeInput("abcdef", 30.0f, 20.0f);  // 30 会软换行
    input.metrics = &metrics;
    input.softWrap = false;
    input.align = Align::Left;

    TextLayoutResult result = LayoutText(input);
    ASSERT_EQ(result.lines.size(), 1u);
    EXPECT_EQ(result.lines[0].text, "abcdef");
    EXPECT_FLOAT_EQ(result.measuredSize.x, 60.0f);  // 6 * 10
    EXPECT_FLOAT_EQ(result.measuredSize.y, 20.0f);
}

TEST(TextLayoutTest, PointTextHardBreakAndCenter) {
    FakeGlyphMetrics metrics;
    TextLayoutInput input = MakeInput("aa\nbbbb", 10.0f, 20.0f);
    input.metrics = &metrics;
    input.softWrap = false;
    input.align = Align::Center;

    TextLayoutResult result = LayoutText(input);
    ASSERT_EQ(result.lines.size(), 2u);
    EXPECT_FLOAT_EQ(result.measuredSize.x, 40.0f);  // longer line "bbbb"
    EXPECT_FLOAT_EQ(result.lines[0].x, 10.0f);      // (40-20)/2
    EXPECT_FLOAT_EQ(result.lines[1].x, 0.0f);
}
```

- [x] **Step 2: 跑测确认失败**

```bash
cmake --build build --target textlayout_tests 2>/dev/null || cmake --build build --target tgfx_adapter_test
# 若测试挂在 textlayout 目标名不同，用：
ctest --test-dir build -R TextLayoutTest --output-on-failure
```

Expected: 编译失败（无 `softWrap`）或断言失败。

- [x] **Step 3: 实现**

在 `TextLayout.h` 增加：

```cpp
bool softWrap = true;  // false = point text: only '\\n', measuredSize is content bounds
```

在 `LayoutAtFontSize`：
- `softWrap==false` 时跳过 `currentWidth + advance > boxWidth` 断行分支。
- 收尾：`maxLineWidth = max(line.width)`（空文本用 0）；`contentHeight` 同现逻辑；
- `softWrap==false`：`measuredSize = {maxLineWidth, contentHeight}`，并用 `maxLineWidth` 重算各行 `x`（Align）。
- `softWrap==true`：保持 `measuredSize = {boxWidth, contentHeight}`（shrink 路径仍覆写为 box 尺寸）。

`LayoutText`：`shrinkToFit` 仅在 `softWrap==true` 时有意义；若 `!softWrap` 强制走非 shrink。

- [x] **Step 4: 跑测确认通过**

```bash
ctest --test-dir build -R TextLayoutTest --output-on-failure
```

Expected: PASS

- [x] **Step 5: Commit**

```bash
git add adapter/textlayout/
git commit -m "$(cat <<'EOF'
Add softWrap point-text layout and content measuredSize.

EOF
)"
```

---

### Task 2: 模型 — 静态 `fontSize` / `size`

**Status:** ✅ Done（序列化一并完成，见 Task 3）

**Files:**
- Modify: `include/MotionStudio/model/TextContent.h`
- Modify: `src/model/PropertyPath.cpp`、`include/MotionStudio/model/PropertyPath.h`
- Modify: `tests/model/TextContentTest.cpp`、`tests/model/PropertyPathTest.cpp`
- 全库替换编译错误：凡 `content.fontSize.evaluate` / `.setStaticValue` / `.isAnimated` → 普通字段读写

**Interfaces:**
- Produces: `float fontSize = 48.0f;`、`Vec2 size{400, 120};`；`ResolveAnimatable` **不再**解析 `content.fontSize` / `content.size`

- [x] **Step 1: 改失败测试**

```cpp
TEST(TextContentTest, DefaultsMatchSpec) {
    TextContent content;
    EXPECT_EQ(content.text.staticValue(), "Text");
    EXPECT_FALSE(content.boxTextMode);
    EXPECT_FLOAT_EQ(content.fontSize, 48.0f);
    EXPECT_FLOAT_EQ(content.size.x, 400.0f);
    EXPECT_FLOAT_EQ(content.size.y, 120.0f);
}

TEST(PropertyPathTest, TextFontSizeAndSizeNotAnimatable) {
    // 建 text layer 后：
    EXPECT_EQ(ResolveAnimatable(document, {textLayer->id, "content.fontSize"}), nullptr);
    EXPECT_EQ(ResolveAnimatable(document, {textLayer->id, "content.size"}), nullptr);
    EXPECT_NE(ResolveAnimatable(document, {textLayer->id, "content.text"}), nullptr);
}
```

- [x] **Step 2: 改模型与 PropertyPath**

```cpp
// TextContent.h
float fontSize = 48.0f;
Vec2 size{400, 120};
// true: PAG box text (wrap + shrink); false: point text
bool boxTextMode = false;
```

删除 `PropertyPath.cpp` 中 `fontSize`/`size` 分支；更新头注释。

- [x] **Step 3: 修编译断裂点（同任务内）**

至少：
- `src/render/SceneEvaluator.cpp`：`textItem.fontSize = textContent.fontSize;`、`containerSize = textContent.size;`
- `src/export/pag/PagFileBuilder.cpp`：去掉 `fontSize`/`size` 的 `isAnimated` / `evaluate` / `CollectKeyframeTimes`
- `tests/**` 中 `setStaticValue` → 直接赋值
- Bridge 测试里对 `content.size` 的 `ms_command_set_static_vec2` 已去掉，Task 4 换专用 API

- [x] **Step 4: 跑测**

```bash
cmake --build build
ctest --test-dir build -R 'TextContentTest|PropertyPathTest|TextLayerEvalTest' --output-on-failure
```

Expected: 所列 PASS；bridge/PAG 相关若仍红，在 Task 4/7 清。

- [x] **Step 5: Commit**

```bash
git add include/MotionStudio/model/ src/model/ src/render/SceneEvaluator.cpp tests/model/ tests/render/
git commit -m "$(cat <<'EOF'
Make TextContent fontSize and size static fields.

EOF
)"
```

---

### Task 3: 序列化静态 `fontSize` / `size`

**Status:** ✅ Done（与 Task 2 同提交，因模型变更必须同步）

**Files:**
- Modify: `src/serialization/Serializer.cpp`
- Modify: `tests/serialization/SerializerTest.cpp`

**Interfaces:**
- Write: `node["fontSize"] = {{"static", fontSize}}`；`node["size"] = {{"static", Vec2ToJson(size)}}`（与现有 static Animatable 外形一致）
- Read: 只接受 `static` 或裸 number/object；**忽略** `keyframes`（若仅有 keyframes 则用默认值或第一个值——选：**有 static 用 static；否则默认 `48` / `{400,120}`**，不报错）

- [x] **Step 1: 更新 round-trip 断言**

确认 `SerializerTest` 文本用例：round-trip 后 `fontSize`/`size` 相等且 JSON 无 `keyframes`。

- [x] **Step 2: 实现读写辅助**（文件内 anonymous namespace）

```cpp
json StaticFloatToJson(float value) { return json{{"static", value}}; }
json StaticVec2ToJson(Vec2 value) { return json{{"static", Vec2ToJson(value)}}; }

Expected<float, std::string> StaticFloatFromJson(const json &node, float fallback);
Expected<Vec2, std::string> StaticVec2FromJson(const json &node, Vec2 fallback);
```

- [x] **Step 3: 跑测**

```bash
ctest --test-dir build -R SerializerTest --output-on-failure
```

Expected: PASS

- [x] **Step 4: Commit**

```bash
git add src/serialization/Serializer.cpp tests/serialization/SerializerTest.cpp
git commit -m "$(cat <<'EOF'
Serialize text fontSize and size as static JSON only.

EOF
)"
```

---

### Task 4: Undo + Bridge — 静态属性与切模式测字

**Status:** ✅ Done

**Files:**
- Create: `include/MotionStudio/undo/SetTextFontSizeCommand.h`、`src/undo/SetTextFontSizeCommand.cpp`
- Create: `include/MotionStudio/undo/SetTextSizeCommand.h`、`src/undo/SetTextSizeCommand.cpp`
- Modify: `SetTextBoxTextModeCommand.*`（开启时可带 `std::optional<Vec2> sizeWhenEnabling`）
- Modify: `CommandKind.h`、CMake 源列表（若显式列举）
- Modify: `bridge/include/motionstudio_bridge.h`、`bridge/src/common/motionstudio_bridge_text.cpp`
- Create/Modify: bridge 测字 helper（建议 `bridge/src/apple/` 或 `adapter/tgfx` 导出 `MeasurePointTextSize(...)`）
- Modify: `tests/undo/TextCommandsTest.cpp`、`bridge/tests/BridgeTest.cpp`

**Interfaces:**
- `SetTextFontSizeCommand(EntityId, float)`
- `SetTextSizeCommand(EntityId, Vec2)`
- `SetTextBoxTextModeCommand(EntityId, bool boxTextMode, std::optional<Vec2> sizeWhenEnabling = nullopt)`  
  - `boxTextMode==true` 且提供 size：execute 时保存 `oldSize_`，写入 `size`，再设 mode；undo 恢复 mode+size  
  - `boxTextMode==false`：只改 mode，不动 size
- Bridge:
  - `bool ms_command_set_text_font_size(MSDocument*, uint64_t layerId, float fontSize);`
  - `bool ms_command_set_text_size(MSDocument*, uint64_t layerId, float w, float h);`
  - `float ms_layer_text_font_size(...)` / `bool ms_layer_text_size(..., float*, float*)`
  - `bool ms_command_set_text_box_text_mode(MSDocument*, uint64_t layerId, bool boxTextMode, int64_t frame);`  
    签名增加 `frame`：当 `false→true` 时用当前文本+字体测点文本尺寸后传入 command

**测字 helper（Apple / tgfx 链接处）：**

```cpp
Vec2 MeasurePointTextSize(const std::string &text, float fontSize, TextAlign align,
                          const std::string &fontFamily, const std::string &fontStyle) {
    // ResolveTextTypeface → TgfxGlyphMetrics → LayoutText(softWrap=false, shrink=false)
    // return max(measuredSize, Vec2{1,1});
}
```

- [x] **Step 1: 写 undo 测试**

```cpp
TEST(TextCommandsTest, EnableBoxTextModeSetsMeasuredSize) {
    // Add text layer, set text "Hi", fontSize 20
    // Execute SetTextBoxTextModeCommand(id, true, Vec2{40, 24});
    // EXPECT_TRUE(content->boxTextMode);
    // EXPECT_FLOAT_EQ(content->size.x, 40);
    // undo → mode false + size restored
}
```

- [x] **Step 2: 实现命令与 bridge**

`ms_command_set_text_box_text_mode`：若目标为 true 且当前为 false，调用 `MeasurePointTextSize`（frame 上 `text.evaluate`），再 `Execute(SetTextBoxTextModeCommand(..., measured))`。

测字经 tgfx adapter 的 `MeasurePointTextSize`（bridge 仅在链 tgfx 时编入）。

- [x] **Step 3: 跑测**

```bash
ctest --test-dir build -R 'TextCommandsTest|BridgeTest' --output-on-failure
```

Expected: PASS（Bridge 测切模式后 size 变化：可用固定 Fake 或只断言 `size` 被改成 ≥1）

- [x] **Step 4: Commit**

```bash
git add include/MotionStudio/undo/ src/undo/ bridge/ tests/undo/ tests/bridge/
git commit -m "$(cat <<'EOF'
Add static text size commands and measure size when enabling box text.

EOF
)"
```

---

### Task 5: Bridge — 点文本 live `containerSize` 覆写

**Status:** ✅ Done

**Files:**
- Modify: `bridge/src/common/motionstudio_bridge_composition.cpp`（hit / bounds / selection_handles）
- Modify: `bridge/src/common/motionstudio_bridge_canvas.cpp`（及 `.mm` 若求值在此）
- Helper: 与 Task 4 共用 `ResolvePointTextContainerSizes(SceneState &state)`

**Interfaces:**
- 对每个 `textItem && !boxTextMode`：用 item 字段测字，设 `containerSize = measured`（≥1）
- 框文本：保持模型 `size`

- [x] **Step 1: 实现 helper 并在三处 Evaluate 成功后调用**

```cpp
void ResolvePointTextContainerSizes(motion::SceneState &state) {
    for (EvaluatedLayer &layer : state.layers) {
        if (!layer.textItem || layer.textItem->boxTextMode) continue;
        auto &item = *layer.textItem;
        item.containerSize = MeasurePointTextSize(
            item.text, item.fontSize, item.align, item.fontFamily, item.fontStyle);
    }
}
```

- [x] **Step 2: 手动/测试验证**（若有 TextLayerEvalTest 只覆盖 Core，可加 bridge 测或 tgfx 测）

点文本长串：handles 宽 ≈ 内容宽，而非 400。

- [x] **Step 3: Commit**

```bash
git add bridge/
git commit -m "$(cat <<'EOF'
Resolve point-text selection bounds from glyph layout.

EOF
)"
```

---

### Task 6: tgfx `drawText` — 去 clip + 点/框布局

**Status:** ✅ Done

**Files:**
- Modify: `adapter/tgfx/src/TgfxCanvasAdapter.cpp`
- Modify: `include/MotionStudio/render/RenderAdapter.h`（注释）
- Modify: `docs/rendering.md`
- Modify: 相关 adapter 测试（若断言 clip/wrap）

**Interfaces:**
- `boxTextMode==false` → `softWrap=false`, `shrinkToFit=false`；允许 `containerSize` 仅作兼容，布局不依赖其宽度
- `boxTextMode==true` → `softWrap=true`, `shrinkToFit=true`, box = containerSize
- **删除** `canvas->clipRect(...)`

```cpp
input.softWrap = boxTextMode;
input.shrinkToFit = boxTextMode;
if (!boxTextMode) {
    input.boxWidth = 1.0f;   // unused when softWrap false; keep >0 for legacy guards
    input.boxHeight = 1.0f;
} else {
    input.boxWidth = containerSize.x;
    input.boxHeight = containerSize.y;
}
// early-return: styles empty / no typeface；点文本不要因 container<=0 直接 return
if (boxTextMode && (containerSize.x <= 0.0f || containerSize.y <= 0.0f)) return;
```

- [x] **Step 1: 改实现 + 注释**
- [x] **Step 2: 跑**

```bash
ctest --test-dir build -R 'TgfxRenderAdapterTest|TextLayoutTest' --output-on-failure
```

- [x] **Step 3: Commit**

```bash
git add adapter/tgfx/ include/MotionStudio/render/RenderAdapter.h docs/rendering.md
git commit -m "$(cat <<'EOF'
Drop text clipRect; map boxTextMode to wrap and shrink.

EOF
)"
```

---

### Task 7: PAG 导出映射

**Status:** ✅ Done

**Files:**
- Modify: `src/export/pag/PagFileBuilder.cpp`（`makeTextDocument`、`buildTextLayer`、`buildSourceText`）
- Modify: `tests/export/pag/PagExporterTest.cpp`
- Modify: `docs/superpowers/specs/2026-07-31-pag-export-design.md` §3.4

**Interfaces:**

```cpp
if (content.boxTextMode) {
    document->boxText = true;
    document->boxTextPos = pag::Point::Zero();
    document->boxTextSize = pag::Point::Make(content.size.x, content.size.y);
    document->firstBaseLine = fontSize * 0.8f;
} else {
    document->boxText = false;
    document->boxTextSize = pag::Point::Zero();
    document->firstBaseLine = 0.0f;
}
```

- 删除 `buildTextLayer` 里 `TextFeatureApproximated`（shrink）warning
- `buildSourceText`：仅当 `content.text.isAnimated()` 时收集 keyframes；`fontSize`/`size` 静态打进每个 document

- [x] **Step 1: 改测试**

```cpp
// Point (default): boxText == false
// Box mode: boxText == true, firstBaseLine != 0, no TextFeatureApproximated for shrink
```

- [x] **Step 2: 实现 + 跑**

```bash
ctest --test-dir build -R PagExporterTest --output-on-failure
```

- [x] **Step 3: Commit**

```bash
git add src/export/pag/ tests/export/pag/ docs/superpowers/specs/2026-07-31-pag-export-design.md
git commit -m "$(cat <<'EOF'
Map boxTextMode directly to PAG boxText export.

EOF
)"
```

---

### Task 8: App — Inspector / 手柄 / Timeline

**Status:** ⏳ Implemented — awaiting commit approval (UI)

**Files:**
- Modify: `apps/MotionStudioApp/.../TextLayerInspector.swift`
- Modify: `apps/MotionStudioApp/.../MotionDocumentCore.swift`
- Modify: `apps/MotionStudioApp/.../FreeTransformDrag.swift`
- Modify: `apps/MotionStudioApp/.../CanvasViewController.swift`
- Modify: `apps/MotionStudioApp/.../TimelineSupport.swift`

**Interfaces / 行为：**

1. **Inspector**
   - Font Size：`showsKeyframeButton: false`；读写 `core.textFontSize` / `setTextFontSize`
   - Width/Height：`isEditable: isEditable && boxTextMode`，`showsKeyframeButton: false`；`setTextSize`
   - Toggle：`setTextBoxTextMode(layerID:boxTextMode:frame:)` 传 playhead

2. **FreeTransformDrag.makeLayerStarts**

```swift
} else if core.layerType(layerID) == .TEXT, core.textBoxTextMode(layerID: layerID) {
    TextProperty.size.path  // 仅框文本；或改用专用 hasTextBoxSize
} else {
    nil
}
```

点文本 `hasContentSize == false` → resize 不写 size（与无容器对象一致）。

3. **CanvasViewController.hitTestHandle**  
   单选点文本时：对 `SCALE_CORNER*` / `SCALE_EDGE*` 直接返回 `.NONE`（仍可 hit 锚点/旋转）。绘制侧若有 scale 方块，同样跳过（搜 `SCALE` / corners 绘制）。

4. **TimelineSupport**  
   去掉 `TextProperty.fontSize` / `.size` 的关键帧轨道枚举（或恒为空）。

5. **MotionDocumentCore.setTextBoxSize**  
   改为 `ms_command_set_text_size` + anchor 补偿（仍可用 merge group）；勿再走 `writeVec2(content.size)`。

- [x] **Step 1: 改 Swift API 封装**
- [x] **Step 2: 改 Inspector / Drag / Canvas / Timeline**
- [x] **Step 3: 用 Xcode MCP 或 `apps/gen_mac` + 编译 MotionStudioApp 验证**
- [x] **Step 4: Commit**

```bash
git add apps/MotionStudioApp/
git commit -m "$(cat <<'EOF'
Gate text resize UI on boxTextMode and drop size keyframes.

EOF
)"
```

---

### Task 9: 文档收尾

**Files:**
- Modify: `docs/rendering.md`（`boxTextMode` 语义、无 clip）
- Modify: `docs/data-model.md`（若描述 Animatable fontSize/size）
- Modify: `docs/superpowers/specs/2026-08-01-point-vs-box-text-design.md`（状态已确认；可链到本 plan）

- [ ] **Step 1: 同步文档与 spec 链接**
- [ ] **Step 2: Commit**

```bash
git add docs/
git commit -m "$(cat <<'EOF'
Sync docs for point vs box text semantics.

EOF
)"
```

---

## Spec Coverage Checklist

| Spec 项 | Task |
|---|---|
| 默认点文本、仅 `\n`、无 soft wrap | 1, 6 |
| 框文本 wrap + shrink | 1, 6 |
| 无 canvas clip | 6 |
| 静态 fontSize/size、无 KF | 2, 3, 4, 8 |
| 点文本无 resize 手柄 / size 置灰 | 8 |
| 点→框测字形设 size | 4 |
| 选中 bounds：点=内容 / 框=size | 5 |
| PAG `boxText` 直映、去 shrink warning | 7 |
| 文档 | 6, 7, 9 |

## Self-Review Notes

- 无 TBD；测字放 bridge/tgfx，避免 Core 依赖字体。
- `TextProperty.size` 路径可保留字符串常量供框文本 `hasProperty` 判断，但 Core `ResolveAnimatable` 不再暴露——App 改用 `textBoxTextMode` 门闩 + 专用 bridge API。
- `NumberPropertyRow` 已有 `showsKeyframeButton`。
