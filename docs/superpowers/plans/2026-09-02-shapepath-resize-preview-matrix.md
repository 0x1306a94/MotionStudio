# ShapePath Resize Preview Matrix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** ShapePath（含 mask）角点/边缩放拖动时用 Canvas 局部预览矩阵，避免每帧 Compile；松手再一次 bake 几何。

**Architecture:** `MSCanvas.previewTransforms` 存局部后乘 `Mat3`；`EnsurePreviewScene` 缓存仍命中；有 preview 时浅拷贝 `SceneState`、改 `worldTransform = world * L`、绕过 `FrameCommandCache` 现场 `BuildCommands`；App 拖动中不写 Document，松手 `beginMergeGroup` → bake → clear preview。

**Tech Stack:** C++17 Core/Bridge、GoogleTest `bridge_test`、Swift App（`FreeTransformDrag` / `CanvasViewController`）

**Spec:** `docs/superpowers/specs/2026-09-02-shapepath-resize-preview-matrix-design.md`

## Global Constraints

- 手柄最终仍禁止写 `transform.scale`（关联 figma-style-resize spec）
- 仅 ShapePath 内容路径 + 同层 mask；Rect/Ellipse/Image/Text/Group 不动
- preview 不进 Document / undo / `contentRevision`
- 局部后乘：`effectiveWorld = worldTransform * L`，`L = T(localPivot)*S(sx,sy)*T(-localPivot)`
- 有 preview 时必须绕过 `FrameCommandCache`（命令已烤死 ConcatTransform）
- `beginMergeGroup` 延后到松手 bake 前
- 空拖动 / cancel：无 undo
- UI 可见改动等用户确认后再 commit（本仓库 git-workflow）

**Status:** 未开始

---

### Task 1: Canvas preview transform API + draw 应用

**Files:**
- Modify: `bridge/src/common/MSCanvas.h`
- Modify: `bridge/include/motionstudio_bridge.h`（在 `ms_canvas_set_view_transform` 附近声明）
- Modify: `bridge/src/common/motionstudio_bridge_canvas.cpp`（实现 API；改 `ms_canvas_draw_frame_at_time_profiled` 及整数帧若转发到同一路径）
- Test: `bridge/tests/BridgeTest.cpp`（或新建 `bridge/tests/CanvasPreviewTransformTest.cpp` 并确保被 `BRIDGE_TEST_SOURCES` 扫到）

**Interfaces:**
- Produces:
  - `void ms_canvas_set_preview_transform(MSCanvas *, uint64_t layer_id, const float m[9]);`
  - `void ms_canvas_clear_preview_transform(MSCanvas *, uint64_t layer_id);`
  - `void ms_canvas_clear_all_preview_transforms(MSCanvas *);`
  - `MSCanvas.previewTransforms: std::unordered_map<motion::EntityId, motion::Mat3>`
- Consumes: `EnsurePreviewScene`, `BuildCommands`, `BuildSelectionOutlineCommands`, `CollectMaskPathOverlays`

- [ ] **Step 1: 写失败测试（无 Metal 也可测状态副作用）**

在 `bridge/tests` 增加用例，覆盖：

1. `set` / `clear` / `clear_all` 对 null canvas 安全 no-op
2. 构造 Document + ShapePath 层；`contentRevision` 在 set preview 前后不变
3. 用 **离屏 / 无 adapter 路径若现有测试惯例不够**：至少断言 map 存取——可把 `previewTransforms` 留在 `MSCanvas.h`（测试已 `#include "MSCanvas.h"` 且 PRIVATE include `src/common`），直接读 `canvas->previewTransforms.size()` 与矩阵值

```cpp
TEST(CanvasPreviewTransformTest, SetClearDoesNotBumpContentRevision) {
    MSDocument *doc = ms_document_create();
    ASSERT_NE(doc, nullptr);
    // 最小 composition + shape path 层（复用 BridgeCommandTest 里 AddShapeLayer / convert 模式）
    MSCanvas canvas = {};  // 无 adapter，仅测状态 API
    const uint64_t before = /* 读 document contentRevision 的既有测试辅助或公开字段 */;
    float m[9] = {2, 0, 0, 0, 2, 0, 0, 0, 1};
    ms_canvas_set_preview_transform(&canvas, /*layerId*/ 1, m);
    ASSERT_EQ(canvas.previewTransforms.size(), 1u);
    EXPECT_FLOAT_EQ(canvas.previewTransforms.begin()->second.values[0], 2.f);
    // contentRevision unchanged when using real MSDocument if API only touches canvas
    ms_canvas_clear_preview_transform(&canvas, 1);
    EXPECT_TRUE(canvas.previewTransforms.empty());
    ms_document_destroy(doc);
}
```

另写一个 **纯函数级** 单测更稳：抽

```cpp
// bridge/src/common/PreviewTransformApply.h（匿名可放 cpp，测试则放 header 或测试可见 free function）
void ApplyPreviewTransformsToScene(motion::SceneState &state,
                                   const std::unordered_map<motion::EntityId, motion::Mat3> &preview);
```

```cpp
TEST(CanvasPreviewTransformTest, ApplyMultipliesWorldOnRight) {
    motion::SceneState state;
    motion::EvaluatedLayer layer;
    layer.id = motion::EntityId{7};
    layer.worldTransform = motion::Mat3::Translate({10, 0});
    state.layers.push_back(layer);
    std::unordered_map<motion::EntityId, motion::Mat3> preview;
    preview[motion::EntityId{7}] = motion::Mat3::Scale({2, 1});
    ApplyPreviewTransformsToScene(state, preview);
    // (T(10,0) * S(2,1)) * point(1,0) => (12, 0)
    const motion::Vec2 p = state.layers[0].worldTransform.transformPoint({1, 0});
    EXPECT_FLOAT_EQ(p.x, 12.f);
    EXPECT_FLOAT_EQ(p.y, 0.f);
}
```

- [ ] **Step 2: 跑测试确认失败**

```bash
cmake --build build --target bridge_test
./build/bridge/bridge_test --gtest_filter='CanvasPreviewTransformTest.*'
```

Expected: 链接失败或未定义 `ms_canvas_set_preview_transform` / `ApplyPreviewTransformsToScene`

- [ ] **Step 3: 实现 API + Apply + draw 分支**

`MSCanvas.h` 增加：

```cpp
#include <unordered_map>
#include "MotionStudio/common/Mat3.h"
// ...
std::unordered_map<motion::EntityId, motion::Mat3> previewTransforms;
```

`motionstudio_bridge.h`（英文注释，对齐现有 canvas API 风格）：

```c
// Local post-multiply preview Mat3 (row-major 9 floats, same layout as motion::Mat3::values).
// effectiveWorld = layer.worldTransform * M. Does not mutate the document.
void ms_canvas_set_preview_transform(MSCanvas *canvas, uint64_t layer_id, const float m[9]);
void ms_canvas_clear_preview_transform(MSCanvas *canvas, uint64_t layer_id);
void ms_canvas_clear_all_preview_transforms(MSCanvas *canvas);
```

实现 set：`layer_id==0` 或 `m==nullptr` 则 no-op；否则写入 `Mat3` 拷贝 `values[9]`。

`ApplyPreviewTransformsToScene`：对每个 `state.layers[i]`，若 map 命中则 `worldTransform = worldTransform * preview`。

在 `ms_canvas_draw_frame_at_time_profiled` 中 `EnsurePreviewScene` 成功后：

```cpp
const bool hasPreview = !canvas->previewTransforms.empty();
motion::SceneState previewState;
const motion::SceneState *drawState = &state;
if (hasPreview) {
    previewState = state;  // 拷贝
    ApplyPreviewTransformsToScene(previewState, canvas->previewTransforms);
    drawState = &previewState;
}
// commands:
const motion::DrawCommandList *commands = nullptr;
motion::DrawCommandList previewCommands;
if (hasPreview) {
    previewCommands = motion::BuildCommands(*drawState);
    commands = &previewCommands;
} else {
    commands = EnsureSceneCommands(canvas, document, compositionId, previewTime, state);
}
// chrome: CollectMaskPathOverlays / BuildSelectionOutlineCommands / path edit 一律用 *drawState
```

整数帧 `ms_canvas_draw_frame_profiled` 若只是转调 at_time，则无需再改。

- [ ] **Step 4: 跑测试确认通过**

```bash
cmake --build build --target bridge_test
./build/bridge/bridge_test --gtest_filter='CanvasPreviewTransformTest.*'
```

Expected: PASS

- [ ] **Step 5: Commit**（用户确认后；本会话默认不自动 commit）

```bash
git commit --only bridge/src/common/MSCanvas.h bridge/include/motionstudio_bridge.h bridge/src/common/motionstudio_bridge_canvas.cpp bridge/src/common/PreviewTransformApply.h bridge/src/common/PreviewTransformApply.cpp bridge/tests/CanvasPreviewTransformTest.cpp -m "Add canvas preview transforms that skip command cache during path resize."
```

（若 Apply 仅放在 canvas cpp 匿名命名空间且测试改走 map 断言，则文件列表以实际为准。）

---

### Task 2: App — ShapePath 拖动走 preview，松手 bake

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Canvas/FreeTransformDrag.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Canvas/CanvasViewController.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift`（薄封装三个 C API，可选）

**Interfaces:**
- Consumes: Task 1 的 `ms_canvas_set/clear(_all)_preview_transform`
- Produces: 拖动中零 Document 写；松手一次 `applyShapeGeometryResize` 语义 bake

- [ ] **Step 1: Core 封装（可选但建议）**

`MotionDocumentCore`：

```swift
func setCanvasPreviewTransform(canvas: OpaquePointer, layerID: UInt64, matrix: [Float]) {
    precondition(matrix.count == 9)
    matrix.withUnsafeBufferPointer { ms_canvas_set_preview_transform(canvas, layerID, $0.baseAddress) }
}
func clearCanvasPreviewTransform(canvas: OpaquePointer, layerID: UInt64) {
    ms_canvas_clear_preview_transform(canvas, layerID)
}
func clearAllCanvasPreviewTransforms(canvas: OpaquePointer) {
    ms_canvas_clear_all_preview_transforms(canvas)
}
```

- [ ] **Step 2: FreeTransformDrag 拆分 preview vs commit**

增加辅助：

```swift
static func usesPathGeometryPreview(_ start: LayerTransformStart) -> Bool {
    start.shapePath != nil || !start.maskPaths.isEmpty
}

static func localPostMultiplyScale(localPivot: CGPoint, scaleX: CGFloat, scaleY: CGFloat) -> [Float] {
    // L = T(pivot) * S(sx,sy) * T(-pivot), row-major Mat3.values
    let px = Float(localPivot.x), py = Float(localPivot.y)
    let sx = Float(scaleX), sy = Float(scaleY)
    // values: [sx, 0, px-sx*px,  0, sy, py-sy*py,  0, 0, 1]
    return [sx, 0, px - sx * px,
            0, sy, py - sy * py,
            0, 0, 1]
}
```

`apply` / `applyScale` 签名扩展为能拿到 `canvas: OpaquePointer?`（或改由 VC 在 apply 后根据返回值设矩阵）。**推荐最小侵入：**

```swift
struct PathPreviewUpdate {
    var layerID: UInt64
    var matrix: [Float]  // 9
}

func apply(...) -> [PathPreviewUpdate]  // 默认 []
```

在 `applyScale` 循环里：

```swift
if Self.usesPathGeometryPreview(start) {
    // 计算 localPivot —— 原样复制 applyShapeGeometryResize 前半段
    let matrix = Self.localPostMultiplyScale(localPivot: localPivot, scaleX: scaleX, scaleY: scaleY)
    updates.append(PathPreviewUpdate(layerID: start.layerID, matrix: matrix))
    // 不写 path / mask / anchor / position
    continue
}
// 原有 Image/Text/Group/Rect 分支不变
```

把现 `applyShapeGeometryResize` 重命名/保留为 `commitShapeGeometryResize`，供松手调用（逻辑不变）。

- [ ] **Step 3: CanvasViewController 生命周期**

`beginHandleTransform`：

```swift
let needsPathPreview = starts.contains { FreeTransformDrag.usesPathGeometryPreview($0) }
if !needsPathPreview {
    document.core.beginMergeGroup()
}
// 若 needsPathPreview：拖动中不开 merge；仍保存 freeTransformDrag
freeTransformDrag = ...
pathResizePreviewActive = needsPathPreview  // 新私有标志
```

`updateFreeTransform`：

```swift
let updates = freeTransformDrag.apply(...)  // 若仍 void，则在 apply 内通过 callback；推荐返回 updates
if let canvas, !updates.isEmpty {
    for u in updates {
        document.core.setCanvasPreviewTransform(canvas: canvas, layerID: u.layerID, matrix: u.matrix)
    }
}
freeTransformDidMove = true
requestDraw()
```

`endFreeTransform`：

```swift
defer {
    if let canvas { document.core.clearAllCanvasPreviewTransforms(canvas: canvas) }
    freeTransformDrag = nil
    freeTransformDidMove = false
    pathResizePreviewActive = false
}
guard let freeTransformDrag else { return }

if pathResizePreviewActive {
    guard freeTransformDidMove else { return }
    document.core.beginMergeGroup()
    // 用最终 scenePoint 再算一次 scale（或缓存 lastScaleX/Y）后:
    freeTransformDrag.commitPathGeometryResizes(core:document.core, frame:evaluationFrame, scaleX:lastScaleX, scaleY:lastScaleY)
    document.core.endMergeGroup()
    registerEdit(freeTransformDrag.editName)
    return
}

// 原路径：已在 begin 开了 merge
document.core.endMergeGroup()
guard freeTransformDidMove else { return }
registerEdit(freeTransformDrag.editName)
```

实现细节：在 `FreeTransformDrag` 内缓存 `lastScaleX/Y`（`applyScale` 末尾写入），`commitPathGeometryResizes` 对每个 `usesPathGeometryPreview` 的 start 调原 `applyShapeGeometryResize`。

**注意 clear 时机：** bake 写 Document 后、`requestDraw` 前 clear；`defer` clear 需在 bake 之后——上面 defer 在函数末尾执行，bake 在 defer 注册后、return 前执行，顺序正确（defer 后执行）。

- [ ] **Step 4: 人机验证（可见 UI，确认前不 commit）**

1. 导入稠密文字转曲 SVG（~200 边）
2. 拖角点：应流畅；选中框跟随
3. 松手：几何变大，scale 仍为 (1,1)；可短顿
4. Undo 一次回到拖前
5. 空点手柄抬起：无 undo
6. 对照：Rect/Image 缩放行为不变

- [ ] **Step 5: Commit**（用户确认 UI 后）

```bash
git commit --only apps/MotionStudioApp/MotionStudioApp/Canvas/FreeTransformDrag.swift apps/MotionStudioApp/MotionStudioApp/Canvas/CanvasViewController.swift apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift docs/superpowers/plans/2026-09-02-shapepath-resize-preview-matrix.md -m "Preview ShapePath resize with canvas matrices and bake on mouse up."
```

---

### Task 3: 文档索引与 plan 状态

**Files:**
- Modify: `docs/README.md`（specs 表加一行）
- Modify: 本 plan 文件勾选与 `**Status:**`

- [ ] **Step 1:** README 在 geometry-revision 条目附近增加：

`| [superpowers/specs/2026-09-02-shapepath-resize-preview-matrix-design.md](superpowers/specs/2026-09-02-shapepath-resize-preview-matrix-design.md) | ShapePath 缩放拖动：Canvas 预览矩阵，松手 bake 几何 |`

并链到 plan（若 README 也列 plans；否则只列 spec）。

- [ ] **Step 2:** 全部 Task 完成后将本 plan `**Status:** ✅ Done`，checkbox 全勾。

- [ ] **Step 3:** 与 Task 2 一并或紧随 commit（文档可与 spec 首次提交合并——见下方提交顺序）。

---

## 提交顺序（本仓库规范）

1. 用户确认 spec+plan 后：**单独** commit 文档（spec + plan + README），再开始编码  
2. Task 1 代码 commit  
3. Task 2 UI：人机确认后再 commit（含 plan 勾选）

用户偏好「不自动 commit」时：每步停下来等明确提交指示。

---

## Spec coverage 自检

| Spec 要求 | Task |
|---|---|
| Canvas `previewTransforms` + 三 API | Task 1 |
| 局部后乘 / EffectiveWorld | Task 1 Apply + Task 2 矩阵构造 |
| 绕过 FrameCommandCache、求值缓存命中 | Task 1 draw 分支 |
| 仅 ShapePath+mask | Task 2 `usesPathGeometryPreview` |
| 拖动不写 Document；merge 延后 | Task 2 VC 生命周期 |
| 松手 bake 复用现数学 | Task 2 `commitShapeGeometryResize` |
| 选中框跟 preview | Task 1 chrome 用 drawState |
| 空拖动无 undo | Task 2 end 分支 |
| 测试 | Task 1 gtest；Task 2 人机 |
| README | Task 3 |
