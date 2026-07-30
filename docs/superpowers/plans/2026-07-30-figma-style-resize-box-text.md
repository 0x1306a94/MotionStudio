# Figma-style Resize + Box Text Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 选中框手柄永不写 `transform.scale`（改 size / 路径几何）；删除图片 container/scale 模式；`autoHeight` 替换为 `boxTextMode`（关=clip，开=缩字号），去掉虚拟测高框。

**Architecture:** Core 文本模型改为固定框 + `boxTextMode`；TextLayout 始终带 `boxHeight`，用 `shrinkToFit` 区分缩字/仅排版；选中框在 App 层按图层类型写 size 或路径；形状/mask 顶点用局部仿射改写，经现有 bezier 属性命令或专用 merge 命令落盘。

**Tech Stack:** C++17 core、GoogleTest、adapter/textlayout、tgfx、C bridge、Swift App。

**Spec:** `docs/superpowers/specs/2026-07-30-figma-style-resize-box-text-design.md`

## Global Constraints

- 不升 `schemaVersion`；JSON 用 `boxTextMode`；**不**读、不迁旧 `autoHeight`（相关代码直接删）。
- 手柄拖动**禁止**写 `transform.scale`；scale 仅属性面板。
- 形状 resize：内容路径（含 ShapePath 顶点；ShapeRect/Ellipse 的 position/size）与 **全部 mask 路径** 同步仿射。
- Core 不依赖 tgfx；排版在 `adapter/textlayout`。
- 提交：Core/adapter/测试可随任务提交；含画布交互的 App 改动可本地验证后再提交（或按用户要求）。

## File Map

| 区域 | 文件 |
|---|---|
| 文本模型 | `include/MotionStudio/model/TextContent.h` |
| Undo | 删 `SetTextAutoHeightCommand.*`；增 `SetTextBoxTextModeCommand.*`；`CommandKind.h` |
| 序列化 | `src/serialization/Serializer.cpp` |
| 求值/指令 | `EvaluatedTextItem.h`、`DrawCommand.h`、`SceneEvaluator.cpp`、`CommandBuilder.cpp`、`HitTest.cpp`、`RenderAdapter.h` |
| 虚拟框 | 删或掏空 `bridge/src/common/TextLayoutHits.*` 及 canvas 调用 |
| 排版 | `adapter/textlayout/include/.../TextLayout.h`、`TextLayout.cpp`、测试 |
| tgfx | `TgfxCanvasAdapter.*` |
| Bridge | `motionstudio_bridge.h`、`motionstudio_bridge_text.cpp` |
| App 文本 | `MotionDocumentCore.swift`、`TextLayerInspector.swift` |
| App 选中框 | `FreeTransformDrag.swift`、`CanvasViewController.swift`、`EditorState.swift`、`EditorViewController+Layout.swift` |
| 形状 resize | 新 undo/bridge API 或复用 bezier 属性命令；`FreeTransformDrag` |
| 文档 | `docs/rendering.md`、本 spec 状态 |

---

### Task 1: 模型 — `boxTextMode` 替换 `autoHeight`

**Files:**
- Modify: `include/MotionStudio/model/TextContent.h`
- Modify: `tests/model/TextContentTest.cpp`

**Interfaces:**
- Produces: `TextContent::boxTextMode`（`bool`，默认 `false`）；删除 `autoHeight`

- [x] **Step 1: 改失败测试**

```cpp
TEST(TextContentTest, DefaultsMatchSpec) {
    TextContent content;
    EXPECT_EQ(content.text.staticValue(), "Text");
    EXPECT_FALSE(content.boxTextMode);
    EXPECT_EQ(content.size.staticValue(), (Vec2{400, 120}));
}
```

删除一切 `EXPECT_TRUE(content.autoHeight)`。

- [x] **Step 2: 改模型**

```cpp
// Font size cap; shrink applies only when boxTextMode is true.
Animatable<float> fontSize{48.0f};
// Fixed layout box; selection / hit bounds use this size.
Animatable<Vec2> size{Vec2{400, 120}};
// true: wrap + shrink font to fit box; false: wrap + clip overflow.
bool boxTextMode = false;
```

- [x] **Step 3: 跑测试**

Run: `ctest --test-dir build -R TextContentTest --output-on-failure`  
（若尚未配置 build：`cmake -B build -G Ninja -DMOTIONSTUDIO_ENABLE_ASAN=ON && cmake --build build`）  
Expected: TextContentTest PASS（其它目标可能暂因 `autoHeight` 编不过，后续任务清引用）。

- [x] **Step 4: Commit**

```bash
git add include/MotionStudio/model/TextContent.h tests/model/TextContentTest.cpp
git commit -m "$(cat <<'EOF'
Replace TextContent.autoHeight with boxTextMode.

EOF
)"
```

---

### Task 2: Undo — `SetTextBoxTextModeCommand`

**Files:**
- Delete: `include/MotionStudio/undo/SetTextAutoHeightCommand.h`、`src/undo/SetTextAutoHeightCommand.cpp`
- Create: `include/MotionStudio/undo/SetTextBoxTextModeCommand.h`、`src/undo/SetTextBoxTextModeCommand.cpp`
- Modify: `include/MotionStudio/undo/CommandKind.h`（`SetTextAutoHeight` → `SetTextBoxTextMode`）
- Modify: `tests/undo/TextCommandsTest.cpp`

**Interfaces:**
- Produces: `SetTextBoxTextModeCommand(EntityId layerId, bool boxTextMode)`；`CommandKind::SetTextBoxTextMode`

- [x] **Step 1: 改测试** 使用 `SetTextBoxTextModeCommand`，断言 `boxTextMode`

- [x] **Step 2: 实现命令**（照抄原 `SetTextAutoHeightCommand`，字段改名）

- [x] **Step 3: 跑测试**

Run: `ctest --test-dir build -R TextCommandsTest --output-on-failure`  
Expected: PASS

- [x] **Step 4: Commit**

```bash
git add include/MotionStudio/undo/ src/undo/ tests/undo/TextCommandsTest.cpp
git commit -m "$(cat <<'EOF'
Add SetTextBoxTextModeCommand and remove autoHeight undo.

EOF
)"
```

---

### Task 3: 序列化 — `boxTextMode` only

**Files:**
- Modify: `src/serialization/Serializer.cpp`（写/读 `boxTextMode`；删除 `autoHeight` 分支）
- Modify: 相关 `tests/serialization/*` 中含 `autoHeight` 的用例

**Interfaces:**
- JSON key: `"boxTextMode"` (bool)；缺省 = `false`
- **不**解析 `"autoHeight"`

- [x] **Step 1: 改 round-trip 测试** 断言 `boxTextMode` 往返

- [x] **Step 2: 改 Serializer** 写/读 `boxTextMode`

- [x] **Step 3: 跑测试**

Run: `ctest --test-dir build -R Serializer --output-on-failure`  
Expected: PASS

- [x] **Step 4: Commit**

```bash
git add src/serialization/Serializer.cpp tests/serialization/
git commit -m "$(cat <<'EOF'
Serialize text boxTextMode; drop autoHeight JSON.

EOF
)"
```

---

### Task 4: 求值 / DrawCommand / Hit — 去掉虚拟测高

**Files:**
- Modify: `include/MotionStudio/render/EvaluatedTextItem.h`（`autoHeight` → `boxTextMode`；删除 `hitSize`，HitTest 用 `containerSize`）
- Modify: `src/render/SceneEvaluator.cpp`、`CommandBuilder.cpp`、`HitTest.cpp`
- Modify: `include/MotionStudio/render/DrawCommand.h`（`textAutoHeight` → `textBoxTextMode`）
- Modify: `include/MotionStudio/render/RenderAdapter.h` `drawText(..., bool boxTextMode, ...)`
- Delete 调用链：`bridge/src/common/TextLayoutHits.cpp/.h` 及 canvas 中测高写回
- Modify: `tests/render/TextLayerEvalTest.cpp`、`CommandBuilderTest.cpp` 等

**Interfaces:**
- Produces: `EvaluatedTextItem::boxTextMode`；选区/hit 只用 `containerSize`
- Consumes: Task 1 模型字段

- [x] **Step 1: 改失败测试** — 不再依赖测高 hitSize；`boxTextMode` 透传

- [x] **Step 2: 改求值与 HitTest；删除 TextLayoutHits 挂钩**

- [x] **Step 3: 跑测试**

Run: `ctest --test-dir build -R 'TextLayerEval|CommandBuilder|HitTest|bridge_test' --output-on-failure`  
Expected: 相关 PASS（adapter 签名可能仍失败 → Task 5）

- [x] **Step 4: Commit**

```bash
git add include/MotionStudio/render/ src/render/ bridge/src/common/ tests/render/
git commit -m "$(cat <<'EOF'
Drive text hit bounds from content.size; remove autoHeight layout hits.

EOF
)"
```

---

### Task 5: TextLayout + tgfx `drawText` 语义

**Files:**
- Modify: `adapter/textlayout/include/MotionStudio/textlayout/TextLayout.h`
- Modify: `adapter/textlayout/src/TextLayout.cpp`
- Modify: `adapter/textlayout/tests/TextLayoutTest.cpp`
- Modify: `adapter/tgfx/include/TgfxCanvasAdapter.h`、`adapter/tgfx/src/TgfxCanvasAdapter.cpp`
- Modify: `adapter/tgfx/tests/TgfxRenderAdapterTest.cpp` 等

**Interfaces:**
- `TextLayoutInput`：

```cpp
struct TextLayoutInput {
    std::string text;
    float boxWidth = 0;
    float boxHeight = 0;       // always fixed box
    bool shrinkToFit = false;  // true = boxTextMode; false = layout at fontSize only
    float fontSize = 48;
    Align align = Align::Left;
    const GlyphMetrics *metrics = nullptr;
};
```

- `LayoutText`：`shrinkToFit == false` → 固定字号排版；`true` → 二分缩字
- `TgfxCanvasAdapter::drawText(..., bool boxTextMode, ...)`：始终设 `boxHeight`，`shrinkToFit = boxTextMode`；**始终 clip** 到容器

- [x] **Step 1: 改 TextLayout 测试**（关=不缩字；开=矮框时 `appliedFontSize < fontSize`）

- [x] **Step 2: 实现 TextLayout + adapter**

- [x] **Step 3: 跑测试**

Run: `ctest --test-dir build -R 'TextLayout|tgfx_adapter' --output-on-failure`  
Expected: PASS

- [x] **Step 4: Commit**

```bash
git add adapter/textlayout/ adapter/tgfx/
git commit -m "$(cat <<'EOF'
Layout fixed text boxes with optional shrinkToFit for boxTextMode.

EOF
)"
```

---

### Task 6: Bridge + Swift Inspector — 框文本模式

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`
- Modify: `bridge/src/common/motionstudio_bridge_text.cpp`
- Modify: `bridge/tests/BridgeTest.cpp`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/TextLayerInspector.swift`

**Interfaces:**

```c
bool ms_command_set_text_box_text_mode(MSDocument *document, uint64_t layerId, bool boxTextMode);
bool ms_layer_text_box_text_mode(MSDocument *document, uint64_t layerId);
```

删除 `ms_command_set_text_auto_height` / `ms_layer_text_auto_height`。新建层默认 `boxTextMode = false`。

- [x] **Step 1: Bridge 测试改用新 API**

- [x] **Step 2: 实现 bridge + Swift + Inspector「框文本模式」**

- [x] **Step 3: 跑测试**

Run: `ctest --test-dir build -R bridge_test --output-on-failure`  
Expected: PASS

- [x] **Step 4: Commit**

```bash
git add bridge/ apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift \
  apps/MotionStudioApp/MotionStudioApp/Inspector/TextLayerInspector.swift
git commit -m "$(cat <<'EOF'
Expose boxTextMode on bridge and text inspector.

EOF
)"
```

---

### Task 7: 选中框 — 禁止 scale；删除 ImageResizeMode

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Canvas/FreeTransformDrag.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Canvas/CanvasViewController.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Model/EditorState.swift`（删 `ImageResizeMode`）
- Modify: `apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController+Layout.swift`
- 其它 `imageResizeMode` / `ImageResizeMode` 引用

**行为：**
- `applyScale`：**删除** 写 `transform.scale` 的分支
- 单选/多选 Image/Text：始终 `applyContainerResize`（有 `contentSizePath`）
- Shape：本任务可先跳过写入（Task 8 接入）
- 无 size 的非 Shape 层：只更新相对 pivot 的 `position`
- 去掉 `imageResizeMode` 与模式分支

- [ ] **Step 1: 删除 UI 与 enum；编译定位引用**

- [ ] **Step 2: 改 `FreeTransformDrag.applyScale` 只走容器/几何，不写 scale**

- [ ] **Step 3: 手动验证** — 图片/文本拖角 size 变、scale 属性不变；模式分段控件消失；面板改 scale 仍有效

- [ ] **Step 4: Commit**

```bash
git add apps/MotionStudioApp/
git commit -m "$(cat <<'EOF'
Make selection handles resize boxes only; remove image scale mode UI.

EOF
)"
```

---

### Task 8: 形状 + mask 路径几何 resize

**Files:**
- Bridge 新 API（建议）：

```c
// Transforms shape geometry + all mask paths in layer-local space about localPivot
// by (scaleX, scaleY). Writes via existing static/playhead path commands inside merge group.
bool ms_command_resize_layer_geometry(MSDocument *document, uint64_t layerId,
                                      double frameTime,
                                      float localPivotX, float localPivotY,
                                      float scaleX, float scaleY);
```

- 实现：`ShapePath` 变换顶点与切线；`ShapeRect`/`ShapeEllipse` 变换 `position`/`size`；每个 `layer.masks[i].path` 同步仿射
- Modify: `FreeTransformDrag` + `MotionDocumentCore` 单选/多选 Shape 调用；并补偿 `position`/`anchor`
- Test: `bridge/tests/BridgeTest.cpp` — 放大后顶点/mask 变、`transform.scale` 不变

仿射：`p' = pivot + (p - pivot) * (sx, sy)`（sx/sy 可负）

- [ ] **Step 1: 写 bridge 测试（路径 + mask 同步）**

- [ ] **Step 2: 实现 API + Swift 封装**

- [ ] **Step 3: 接入 `FreeTransformDrag` 单选/多选**

- [ ] **Step 4: 跑测试 + 手动拖路径层/带 mask 层**

Run: `ctest --test-dir build -R bridge_test --output-on-failure`

- [ ] **Step 5: Commit**

```bash
git add bridge/ apps/MotionStudioApp/MotionStudioApp/Canvas/FreeTransformDrag.swift \
  apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift
git commit -m "$(cat <<'EOF'
Resize shape and mask paths from selection handles without changing scale.

EOF
)"
```

---

### Task 9: 文档与扫尾

**Files:**
- Modify: `docs/rendering.md`
- Grep 清零：`autoHeight` / `ImageResizeMode` / `textAutoHeight` / `TextLayoutHits` / `SetTextAutoHeight`（`third_party/` 与历史 plan 除外）
- Inspector `setTextBoxSize`：保持「只等比 anchor、不动 position」（与手柄对角固定区分）

- [ ] **Step 1: Grep 清零残留代码引用**

```bash
rg -n 'autoHeight|ImageResizeMode|TextLayoutHits|textAutoHeight|SetTextAutoHeight' \
  --glob '!third_party/**' --glob '!docs/superpowers/plans/2026-07-30-text-layer.md'
```

- [ ] **Step 2: 更新 `docs/rendering.md`**

- [ ] **Step 3: 全量测试**

Run: `ctest --test-dir build --output-on-failure -LE benchmark`  
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add docs/
git commit -m "$(cat <<'EOF'
Document boxTextMode and Figma-style handle resize.

EOF
)"
```

---

## Spec coverage checklist

| Spec 项 | Task |
|---|---|
| 手柄不写 scale | 7 |
| 单选/多选 Image/Text 改 size | 7 |
| 删除 ImageResizeMode | 7 |
| Shape + mask 几何 resize | 8 |
| boxTextMode 模型/序列化/undo | 1–3 |
| 删 autoHeight、无迁移 | 1–6、9 |
| 去虚拟测高 | 4 |
| 关=clip / 开=shrink | 5 |
| Bridge + Inspector | 6 |
| 文档 | 9 |

---

Plan complete and saved to `docs/superpowers/plans/2026-07-30-figma-style-resize-box-text.md`.

**执行方式二选一：**

1. **Subagent-Driven（推荐）** — 每任务新 subagent，任务间 review  
2. **Inline Execution** — 本会话按计划连续执行  

选哪个？
