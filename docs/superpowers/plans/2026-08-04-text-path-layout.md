# Text Path Layout Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **每完成一个 Step/Task 必须立刻把本文件对应勾选改为 `[x]` 并更新 Task Status，随 commit 提交（见 AGENTS.md「按 plan 实现」）。**

**Goal:** 点文本沿用户绘制的路径图层排布（环形 / wave 等由路径形状表达），含选中 AABB、Adapter 缓存与 PAG `TextPathOptions` 导出。

**Architecture:** Core `TextContent.textPath` 只存参数与 path 层引用；`SceneEvaluator` 将路径变换到文本 local 写入 `EvaluatedTextItem` / `TextDrawParams`；排版在 `adapter/tgfx` 用 **tgfx `PathMeasure`**（对照参考 libpag `TextPathRender` 源码，**不链接** libpag 渲染层）；绘制与 AABB 共用布局缓存。

**Tech Stack:** C++17 core、`adapter/tgfx` + 预编译 Metal tgfx、`adapter/textlayout`、Apple C bridge、GoogleTest、SwiftUI、`pag_codec` / `PagFileBuilder`。

**Spec:** `docs/superpowers/specs/2026-08-04-text-path-layout-design.md`

## Global Constraints

- Core **不做**弧长排版；只解析 path + 坐标系变换。
- **不链接** libpag `TextPathRender` / 整库 rendering（无 Metal）；算法只读对照。
- 仅点文本：`textPath` 有效时 Adapter 忽略 `boxTextMode`（不改写存储值）。
- 路径层 `visible=false` **仍**参与布局。
- 自引用 / 无效 id / 无 path → 退回普通点文本。
- margin 可 KF；`reversed` / `perpendicular` / `forceAlignment` 静态。
- 导出：未 reverse 的文本 local 几何 + `reversedPath` = 模型值；path 进 `pathOption`，不进普通 masks。
- `textlayout` 目标保持无 tgfx 依赖；`PathMeasure` 实现放 `adapter/tgfx`。
- Commit：英语、≤120 字符、句号结尾、句中无其他标点；`git commit --only`；不 push。
- 构建：Ninja + ASan 测 Core；App 优先 Xcode MCP。

## File Map

| 区域 | 文件 |
|---|---|
| 模型 | Create: `include/MotionStudio/model/TextPath.h`；Modify: `TextContent.h` |
| Undo | Create: `SetTextPathCommand.h/.cpp`；Modify: `CommandKind.h` |
| PropertyPath | Modify: `src/model/PropertyPath.cpp`（`content.textPath.firstMargin` / `lastMargin`） |
| 序列化 | Modify: `src/serialization/Serializer.cpp` |
| 路径变换 | Create 或扩: `include/MotionStudio/common/BezierPathTransform.h` + cpp（`TransformBezierPath`） |
| 求值 | Modify: `EvaluatedTextItem.h`、`SceneEvaluator.cpp`；复用 `EvaluateLayerPath` |
| Draw | Create: `TextDrawParams.h`；Modify: `DrawCommand.h`、`RenderAdapter.h/.cpp`、`CommandBuilder.cpp` |
| 布局 | Create: `adapter/tgfx/.../TextPathLayout.h/.cpp`、`MeasureTextPathBounds` |
| 绘制缓存 | Modify: `TgfxCanvasAdapter.cpp/.h` |
| AABB | Modify: `BridgeInternals.cpp`（`ResolvePointTextContainerSizes`）、`HitTest.cpp`（`BoundsOfLayerLocal`） |
| Bridge | Modify: `motionstudio_bridge.h`、commands/text bridge cpp |
| App | Create: `TextPathInspector.swift`；Modify: `TextLayerInspector`、`MotionDocumentCore`、`PropertyPath.swift`、`TimelineSupport.swift`、`InspectorView.swift` |
| PAG | Modify: `PagFileBuilder.cpp`、pag export tests、`2026-07-31-pag-export-design.md` §3.4 |
| 本 plan / spec | 同步勾选与状态 |

---

### Task 1: `TextPath` 模型 + PropertyPath + 序列化 + `SetTextPathCommand`

**Status:** ✅ Done

**Files:**
- Create: `include/MotionStudio/model/TextPath.h`
- Modify: `include/MotionStudio/model/TextContent.h` — 增加 `TextPath textPath;`
- Modify: `include/MotionStudio/undo/CommandKind.h` — `SetTextPath`
- Create: `include/MotionStudio/undo/SetTextPathCommand.h`、`src/undo/SetTextPathCommand.cpp`
- Modify: `src/model/PropertyPath.cpp` — `content.textPath.firstMargin` / `lastMargin`（三段路径）
- Modify: `src/serialization/Serializer.cpp` — text content `textPath` 对象
- Test: `tests/model/PropertyPathTest.cpp`、`tests/serialization/` 或 `tests/undo/` 新增用例；注册到 `core_tests` CMake

**Interfaces:**
- Consumes: 现有 `Animatable<float>`、`EntityId`、`Serializer` text content 写法
- Produces:
```cpp
struct TextPath {
    bool enabled = false;
    EntityId pathLayerId;
    bool reversed = false;
    bool perpendicular = true;
    bool forceAlignment = false;
    Animatable<float> firstMargin{0.f};
    Animatable<float> lastMargin{0.f};
};
// SetTextPathCommand(layerId, enabled, pathLayerId, reversed, perpendicular, forceAlignment)
// 自引用 / 无效 pathLayerId → enabled=false, pathLayerId 清空（同 Follow Path）
```

- [x] **Step 1: Write failing tests**

```cpp
TEST(ResolveAnimatableTest, ResolvesTextPathMargins) {
    // Text layer with TextContent
    EXPECT_NE(ResolveAnimatable(doc, {textLayerId, "content.textPath.firstMargin"}), nullptr);
    EXPECT_NE(ResolveAnimatable(doc, {textLayerId, "content.textPath.lastMargin"}), nullptr);
    EXPECT_EQ(ResolveAnimatable(doc, {textLayerId, "content.textPath.enabled"}), nullptr);
}

TEST(SetTextPathCommandTest, RejectsSelfReference) {
    // execute enabled=true, pathLayerId=self → enabled false
}

TEST(SerializerTextPathTest, RoundTrip) {
    // set fields + margin keyframe → serialize → deserialize → equal
}
```

- [x] **Step 2: Run tests — expect FAIL**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='*TextPath*'
```

- [x] **Step 3: Implement model, command, PropertyPath, serialization**

`PropertyPath` 在 `first.name == "content"` 时支持：
- `segments.size()==2 && name=="text"`（现状）
- `segments.size()==3 && segments[1]=="textPath" && (firstMargin|lastMargin)`

- [x] **Step 4: Tests PASS + commit**

```bash
git commit --only <files> -m "Add TextPath model serialization and set command."
```

---

### Task 2: `TransformBezierPath` + 求值挂到 `EvaluatedTextItem`

**Status:** ✅ Done

**Files:**
- Create: `include/MotionStudio/common/BezierPathTransform.h`、`src/common/BezierPathTransform.cpp`（若项目更习惯放 `render/`，可放 `FollowPathEval` 旁，但勿拉高依赖）
- Modify: `include/MotionStudio/render/EvaluatedTextItem.h`
- Modify: `src/render/SceneEvaluator.cpp` — 填 `textItem.textPath`
- Test: `tests/render/TextPathEvalTest.cpp`（新建）

**Interfaces:**
- Consumes: `EvaluateLayerPath(layer, time)`（`FollowPathEval.h`）、`Layer::worldTransform` / evaluator 已有 world
- Produces:
```cpp
BezierPath TransformBezierPath(const BezierPath &path, const Mat3 &matrix);
// point' = matrix * point；in/outTangent 用 matrix.transformVector（去平移）

struct EvaluatedTextPath {
    BezierPath path;       // 文本 local，未 reverse
    bool reversed = false;
    bool perpendicular = true;
    bool forceAlignment = false;
    float firstMargin = 0;
    float lastMargin = 0;
};
// EvaluatedTextItem::std::optional<EvaluatedTextPath> textPath;
// 另：localBoundsMin/Max + useExactLocalBounds（Task 6 填；本任务可先默认 false）
```

求值（在 text 分支，world 已算完后）：
```
if content.textPath.enabled:
  if pathLayerId==self or !valid → skip
  pathLayer = find（不检查 visible）
  optPath = EvaluateLayerPath(*pathLayer, time)
  if !optPath → skip
  M = inv(text.world) * pathLayer.world
  evaluated.textPath = { TransformBezierPath(*optPath, M), flags, margins.evaluate }
```

注意：隐藏路径层可能未出现在 `state.layers` 绘制列表——求值必须从 **Document/EntityIndex** 取 path 层，并用与 Follow Path 相同的 world 变换 API（`FollowAwareWorldTransform` / 现有 world 计算），**不要**依赖「仅可见层」列表。

- [x] **Step 1: Failing tests** — 水平 path 层在 (0,0)-(100,0)；文本层 identity → local 端点；path 层 `position` 平移后 local 点变化；`pathLayer.visible=false` 仍有 `textPath`

- [x] **Step 2: Implement transform + SceneEvaluator wiring**

- [x] **Step 3: PASS + commit** `Evaluate text path geometry into text-local space.`

---

### Task 3: `TextDrawParams` 收拢 DrawText 接口

**Status:** ✅ Done

**Files:**
- Create: `include/MotionStudio/render/TextDrawParams.h`
- Modify: `DrawCommand.h` — `DrawText` 携带 `TextDrawParams textParams`（或扁平同步字段，优先内嵌 struct）
- Modify: `RenderAdapter.h`、`src/render/RenderAdapter.cpp`（PlayCommands）
- Modify: `CommandBuilder.cpp` `AppendTextItem`
- Modify: `TgfxCanvasAdapter.h/.cpp` 及所有 `drawText` 覆写处
- 修编译：旧多参数 `drawText` 一律改为 `drawText(const TextDrawParams &)`

**Interfaces:**
```cpp
struct TextDrawParams {
    std::string text;
    float fontSize = 48;
    Vec2 containerSize;
    bool boxTextMode = false;
    TextAlign align = TextAlign::Left;
    std::string fontFamily;
    std::string fontStyle;
    std::vector<TextDrawStyle> styles;
    bool textPathEnabled = false;
    BezierPath textPath;
    bool textPathReversed = false;
    bool textPathPerpendicular = true;
    bool textPathForceAlignment = false;
    float textPathFirstMargin = 0;
    float textPathLastMargin = 0;
};
virtual void drawText(const TextDrawParams &params) = 0;
```

`AppendTextItem`：从 `EvaluatedTextItem` 填满 `TextDrawParams`（含 optional textPath）。

- [x] **Step 1: Refactor compile-green（行为不变，path 字段默认关）**
- [x] **Step 2: 现有 text / tgfx / bridge 相关测试仍绿**
- [x] **Step 3: Commit** `Collapse DrawText into TextDrawParams.`

---

### Task 4: `TextPathLayout`（tgfx PathMeasure）+ 单测

**Status:** ✅ Done

**Files:**
- Create: `adapter/tgfx/include/TextPathLayout.h`（或 `adapter/tgfx/include/MotionStudio/...` 对齐现有）
- Create: `adapter/tgfx/src/TextPathLayout.cpp`
- Create: `adapter/tgfx/tests/TextPathLayoutTest.cpp`（挂 `tgfx_adapter_test`）
- 对照只读：`third_party/libpag/src/rendering/renderers/TextPathRender.cpp`

**Interfaces:**
```cpp
namespace motion {
struct TextPathLayoutInput { /* 见 spec §3；含 GlyphMetrics* 或 Typeface */ };
struct TextPathGlyph {
    std::string utf8;   // 或 GlyphID；实现选一种并固定
    Mat3 matrix;
    float advance = 0;
};
struct TextPathLayoutResult {
    std::vector<TextPathGlyph> glyphs;
    Vec2 boundsMin;
    Vec2 boundsMax;
};
TextPathLayoutResult LayoutTextOnPath(const TextPathLayoutInput &input);
}
```

算法要点（对照 PAG，自研实现）：
1. 用现有 `textlayout::LayoutText`（`softWrap=false`）得每字沿基线的 x / 行 y
2. `reversed` → reverse path 副本再 `tgfx::PathMeasure::MakeFrom`
3. BezierPath → tgfx::Path（复用 adapter 已有转换）
4. forceAlignment / 两端延长 / getPosTan / perpendicular 旋转 — 对齐 PAG 公式
5. bounds = 各 glyph 轴对齐盒并集（可用 font metrics 近似：advance × ascent/descent）

- [x] **Step 1: Failing test** — 水平 path 上单行 "AB"：两字 matrix 平移递增；`perpendicular=true` 时切线水平 → 旋转≈0
- [x] **Step 2: Implement LayoutTextOnPath**
- [x] **Step 3: PASS + commit** `Add TextPathLayout using tgfx PathMeasure.`

---

### Task 5: Adapter 绘制路径 + 单槽缓存

**Status:** ✅ Done

**Files:**
- Modify: `adapter/tgfx/src/TgfxCanvasAdapter.cpp`（及 on-screen 若共用基类）
- 缓存成员：上一帧 key + `TextPathLayoutResult`（单槽即可）

**Interfaces:**
- Consumes: `LayoutTextOnPath`、`TextDrawParams`
- Produces: `drawText` path 分支；`cacheHitsForTest()` 可选（测缓存）

```
drawText(p):
  if p.textPathEnabled && !p.textPath.vertices.empty():
    key = hash(p)
    if key != cachedKey: cached = LayoutTextOnPath(...); cachedKey = key
    for style / glyph: save; concat(glyph.matrix); drawGlyph/TextBlob; restore
  else: 现有直线路径
```

- [x] **Step 1: Wire path branch（可先无 cache）使画布可见路径文本**
- [x] **Step 2: Add single-slot cache + unit/integration assert 同输入二次 hit**
- [x] **Step 3: Commit** `Draw text on path with layout cache in tgfx adapter.`

---

### Task 6: 选中 AABB（路径布局 bounds）

**Status:** ✅ Done

**Files:**
- Create/Modify: `adapter/tgfx/include/MeasureTextPathBounds.h` + cpp（或并入 TextPathLayout）
- Modify: `include/MotionStudio/render/EvaluatedTextItem.h` — `useExactLocalBounds` / `localBoundsMin` / `localBoundsMax`
- Modify: `bridge/src/common/BridgeInternals.cpp` — `ResolvePointTextContainerSizes` 改名为更贴切或扩职责：`ResolveTextLayoutBounds`
- Modify: `src/render/HitTest.cpp` — `BoundsOfLayerLocal` / hit-test 文本分支使用 exact bounds
- Modify: selection / world bounds 若同样读 `containerSize` 且假设原点 0，一并改

**逻辑:**
```
if textItem.textPath:
  r = MeasureTextPathBounds(...)  // 与绘制同一 LayoutTextOnPath
  item.useExactLocalBounds = true
  item.localBoundsMin/Max = r.bounds*
  item.containerSize = {max.x-min.x, max.y-min.y}  // 兼容旧读法
elif !boxTextMode:
  MeasurePointTextSize → min=0, max=size
```

`BoundsOfLayerLocal`:
```
if text && useExactLocalBounds: min/max = localBounds*; else 现状 0..containerSize
```

- [x] **Step 1: Failing bridge/core test** — 圆弧路径文本 local bounds ≠ 直线点文本盒
- [x] **Step 2: Implement + PASS**
- [x] **Step 3: Commit** `Use path-layout bounds for text selection AABB.`

---

### Task 7: Bridge API

**Status:** ⏳ Pending

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`
- Modify: `bridge/src/common/motionstudio_bridge_commands.cpp`（或 text 专用 cpp）
- Modify: `bridge/tests/BridgeTest.cpp`

**Interfaces:**
```c
void ms_command_set_text_path(MSDocument*, uint64_t layerId, bool enabled,
    uint64_t pathLayerId, bool reversed, bool perpendicular, bool forceAlignment);
bool ms_layer_text_path_enabled(MSDocument*, uint64_t layerId);
uint64_t ms_layer_text_path_layer_id(...);
bool ms_layer_text_path_reversed(...);
bool ms_layer_text_path_perpendicular(...);
bool ms_layer_text_path_force_alignment(...);
// margins: 现有 ms_property_* + "content.textPath.firstMargin"
```

- [ ] **Step 1: Bridge tests** — set/get + 自引用清空
- [ ] **Step 2: Implement + PASS**
- [ ] **Step 3: Commit** `Expose text path controls on the C bridge.`

---

### Task 8: App Inspector + Timeline

**Status:** ⏳ Pending

**Files:**
- Create: `apps/MotionStudioApp/MotionStudioApp/Inspector/TextPathInspector.swift`（对照 `FollowPathInspector.swift`）
- Modify: `TextLayerInspector.swift` — 嵌入 Text Path；路径有效时灰显 boxTextMode / size
- Modify: `InspectorView.swift` — 文本层挂载
- Modify: `MotionDocumentCore.swift` — wrappers
- Modify: `Bridge/PropertyPath.swift` — `TextPathProperty` first/lastMargin
- Modify: `Timeline/Root/TimelineSupport.swift` — 有 KF 时露出 margin 轨
- Xcode：把新 Swift 文件加入 app target（MCP 或工程文件）

**UI:**
- Toggle Enabled、Path Layer picker（同 composition、SHAPE、≠self）、Reversed / Perpendicular / Force Alignment
- firstMargin / lastMargin 行（复用 float + keyframe 行模式）
- 候选层：有 path 的 shape（`hasBezierPath` 或 layerType==SHAPE，同 Follow Path）

- [ ] **Step 1: Wire Core wrappers + Inspector UI**
- [ ] **Step 2: Xcode MCP BuildProject（MotionStudioApp）**
- [ ] **Step 3: 人机烟测** — 画圆路径、绑文本、隐藏路径层、选中框贴字形
- [ ] **Step 4: Commit** `Add Text Path inspector and timeline margin tracks.`

---

### Task 9: PagExporter `pathOption` + 文档

**Status:** ⏳ Pending

**Files:**
- Modify: `src/export/pag/PagFileBuilder.cpp` — `buildTextLayer`
- Modify: `tests/export/pag/PagExporterTest.cpp`
- Modify: `docs/superpowers/specs/2026-07-31-pag-export-design.md` §3.4
- Modify: spec 进度若需

**逻辑（在 `buildTextLayer`）：**
```
if content.textPath.enabled && resolvable:
  // 路径有效时按点文本导出 TextDocument（忽略 boxTextMode）
  localPath = 与预览相同变换（未 reverse）
  // maskPath：静态或按路径/相对变换 KF 采样 bake
  pathOption = new TextPathOptions{
    path: MaskData{ maskPath = localPath },  // 勿 push 到 layer.masks
    reversedPath / perpendicular / forceAlignment 静态 Property<bool>
    firstMargin / lastMargin 从 Animatable 映射
  }
  pagLayer->pathOption = pathOption
else if enabled but unresolved:
  warning TextPathUnresolved
```

- [ ] **Step 1: Failing export test** — Load 后 `pathOption != nullptr`，margins / flags 正确
- [ ] **Step 2: Implement + PASS**
- [ ] **Step 3: Update pag-export-design §3.4 + commit** `Export text path options into PAG pathOption.`

---

### Task 10: 收尾验证

**Status:** ⏳ Pending

- [ ] **Step 1:** `ctest --test-dir build --output-on-failure -LE benchmark`（或至少 core/bridge/tgfx/pag 相关）
- [ ] **Step 2:** Spec 状态改为「已落地」；本 plan 全部 Task ✅
- [ ] **Step 3: Commit** `Mark text path layout plan complete.`

---

## Self-Review (plan vs spec)

| Spec 要求 | Task |
|---|---|
| Core 只存参数 | T1 |
| 引用路径层 + 坐标系 | T2 |
| 隐藏路径层仍布局 | T2 测试 |
| TextDrawParams | T3 |
| tgfx PathMeasure 布局 + 缓存 | T4–T5 |
| 不链 libpag 渲染 | Global + T4 |
| 选中 AABB | T6 |
| Bridge + App | T7–T8 |
| PAG pathOption 完整字段 | T9 |
| 超长对齐 PAG | T4 算法 |

无 TBD；类型名前后一致（`TextPath` / `EvaluatedTextPath` / `TextDrawParams` / `LayoutTextOnPath`）。
