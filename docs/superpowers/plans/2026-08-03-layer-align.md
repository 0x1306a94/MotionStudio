# Layer Align Toolbar Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. **每完成一个 Step/Task 必须立刻把本文件对应勾选改为 `[x]` 并更新 Task Status，随 commit 提交（见 AGENTS.md「按 plan 实现」）。**

**Goal:** 在 topToolbar（Inspector 开关左侧）提供 6 向图层对齐；单选对齐合成画幅，多选对齐选中层合成空间视觉 AABB 并集；一次对齐一个 undo merge。

**Architecture:** Swift `LayerAlign` 纯函数算并集与合成空间 `Δ`；Bridge `ms_layer_map_composition_delta` 用父级世界矩阵线性逆把 `Δ` 映到父空间；`MotionDocumentCore.alignLayers` 聚合 bounds/写 position；`EditorViewController` toolbar 接线并 `beginMergeGroup`/`endMergeGroup` + `perform`。

**Tech Stack:** C++17 bridge、GoogleTest、`MotionDocumentCore`、UIKit toolbar、Swift Testing。

**Spec:** `docs/superpowers/specs/2026-08-03-layer-align-design.md`

## Global Constraints

- 入口：`topToolbar` 右侧，`inspectorToggleButton` **紧前**；无选中隐藏。
- 包围盒：`ms_composition_layer_bounds`（合成空间视觉 AABB）。
- 锁定/隐藏：对齐时忽略，仍参与并移动。
- 只改存储 `transform.position`；不动 anchor/scale/rotation。
- Undo：调用方 `beginMergeGroup` → 写各层 → `endMergeGroup`，再 `perform("Align …")`；多选一次 Undo 全部还原。
- 不做 Distribute、Inspector 对齐行、快捷键。
- 提交：每任务结束 commit（不推送）；英语一句、句号结尾、无其它标点。
- 新 Swift 文件放 `apps/MotionStudioApp/MotionStudioApp/` 合适子目录（folder 引用，一般无需改 pbxproj）。

## File Map

| 区域 | 文件 |
|---|---|
| 纯逻辑 | Create: `apps/MotionStudioApp/MotionStudioApp/Editor/LayerAlign.swift` |
| Swift 测试 | Create: `apps/MotionStudioApp/MotionStudioAppTests/LayerAlignTests.swift` |
| Bridge API | Modify: `bridge/include/motionstudio_bridge.h`、`bridge/src/common/motionstudio_bridge_composition.cpp`（或 layer.cpp） |
| Bridge 测试 | Modify: `bridge/tests/BridgeTest.cpp` |
| Core 门面 | Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift` |
| Toolbar | Modify: `EditorViewController.swift`、`EditorViewController+Layout.swift`、`EditorViewController+Commands.swift` |
| Spec 状态 | Modify: `docs/superpowers/specs/2026-08-03-layer-align-design.md` |

---

### Task 1: `LayerAlign` 纯函数 + 测试

**Status:** ⏳

**Files:**
- Create: `apps/MotionStudioApp/MotionStudioApp/Editor/LayerAlign.swift`
- Create: `apps/MotionStudioApp/MotionStudioAppTests/LayerAlignTests.swift`

**Interfaces:**
- Consumes: 无
- Produces:
  - `enum LayerAlignEdge { left, horizontalCenter, right, top, verticalCenter, bottom }`
  - `enum LayerAlign`
  - `static func unionBounds(_ rects: [CGRect]) -> CGRect?`
  - `static func compositionDelta(edge: LayerAlignEdge, bounds: CGRect, target: CGRect) -> CGVector`

- [ ] **Step 1: Write the failing tests**

```swift
// LayerAlignTests.swift
import CoreGraphics
@testable import MotionStudio
import Testing

@MainActor
struct LayerAlignTests {
    @Test
    func `union bounds of two rects`() {
        let a = CGRect(x: 0, y: 0, width: 10, height: 10)
        let b = CGRect(x: 20, y: 30, width: 10, height: 10)
        let u = LayerAlign.unionBounds([a, b])
        #expect(u == CGRect(x: 0, y: 0, width: 30, height: 40))
    }

    @Test
    func `union bounds empty is nil`() {
        #expect(LayerAlign.unionBounds([]) == nil)
    }

    @Test
    func `six edges composition delta`() {
        let bounds = CGRect(x: 10, y: 20, width: 40, height: 60)
        let target = CGRect(x: 0, y: 0, width: 200, height: 100)
        #expect(LayerAlign.compositionDelta(edge: .left, bounds: bounds, target: target)
            == CGVector(dx: -10, dy: 0))
        #expect(LayerAlign.compositionDelta(edge: .horizontalCenter, bounds: bounds, target: target)
            == CGVector(dx: 70, dy: 0)) // midX 100 - 30
        #expect(LayerAlign.compositionDelta(edge: .right, bounds: bounds, target: target)
            == CGVector(dx: 150, dy: 0)) // 200 - 50
        #expect(LayerAlign.compositionDelta(edge: .top, bounds: bounds, target: target)
            == CGVector(dx: 0, dy: -20))
        #expect(LayerAlign.compositionDelta(edge: .verticalCenter, bounds: bounds, target: target)
            == CGVector(dx: 0, dy: -0)) // midY 50 - 50
        // verticalCenter: target.midY=50, bounds.midY=50 → 0
        #expect(LayerAlign.compositionDelta(edge: .bottom, bounds: bounds, target: target)
            == CGVector(dx: 0, dy: 20)) // 100 - 80
    }

    @Test
    func `already aligned yields zero`() {
        let bounds = CGRect(x: 0, y: 0, width: 50, height: 50)
        let target = CGRect(x: 0, y: 0, width: 200, height: 200)
        #expect(LayerAlign.compositionDelta(edge: .left, bounds: bounds, target: target) == .zero)
        #expect(LayerAlign.compositionDelta(edge: .top, bounds: bounds, target: target) == .zero)
    }
}
```

（若 `==` 对 `CGVector`/`CGRect` 浮点不稳，改用分量容差。）

- [ ] **Step 2: Run tests to verify they fail**

优先 Xcode MCP `RunSomeTests`（`MotionStudioAppTests` / `LayerAlignTests`）；不可用则 xcodebuild `-only-testing:MotionStudioAppTests/LayerAlignTests`。

Expected: 编译失败（类型未定义）。

- [ ] **Step 3: Implement `LayerAlign.swift`**

```swift
import CoreGraphics
import Foundation

enum LayerAlignEdge {
    case left, horizontalCenter, right
    case top, verticalCenter, bottom
}

enum LayerAlign {
    static func unionBounds(_ rects: [CGRect]) -> CGRect? {
        guard let first = rects.first else { return nil }
        return rects.dropFirst().reduce(first) { $0.union($1) }
    }

    static func compositionDelta(edge: LayerAlignEdge,
                                 bounds: CGRect,
                                 target: CGRect) -> CGVector
    {
        switch edge {
        case .left:
            return CGVector(dx: target.minX - bounds.minX, dy: 0)
        case .horizontalCenter:
            return CGVector(dx: target.midX - bounds.midX, dy: 0)
        case .right:
            return CGVector(dx: target.maxX - bounds.maxX, dy: 0)
        case .top:
            return CGVector(dx: 0, dy: target.minY - bounds.minY)
        case .verticalCenter:
            return CGVector(dx: 0, dy: target.midY - bounds.midY)
        case .bottom:
            return CGVector(dx: 0, dy: target.maxY - bounds.maxY)
        }
    }
}
```

- [ ] **Step 4: Run tests to verify they pass**

同 Step 2。Expected: PASS。

- [ ] **Step 5: Update this plan** — Task 1 全部 `[x]`，`Status: ✅ Done`。

- [ ] **Step 6: Commit**

```bash
git commit --only \
  apps/MotionStudioApp/MotionStudioApp/Editor/LayerAlign.swift \
  apps/MotionStudioApp/MotionStudioAppTests/LayerAlignTests.swift \
  docs/superpowers/plans/2026-08-03-layer-align.md \
  -m "Add LayerAlign helpers for composition-space edge deltas."
```

---

### Task 2: Bridge `ms_layer_map_composition_delta` + 测试

**Status:** ⏳

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`（`ms_composition_layer_bounds` 附近）
- Modify: `bridge/src/common/motionstudio_bridge_composition.cpp`
- Modify: `bridge/tests/BridgeTest.cpp`

**Interfaces:**
- Consumes: `Layer::worldTransform`、`Mat3::tryInvert`、`Mat3::transformVector`、`FindLayer` / `FindComposition`
- Produces: `bool ms_layer_map_composition_delta(MSDocument*, uint64_t compositionId, uint64_t layerId, double frameTime, float dx, float dy, float* outParentDx, float* outParentDy)`

- [ ] **Step 1: Write the failing test**

在 `BridgeTest.cpp` 追加：

```cpp
TEST(BridgeCompositionTest, MapCompositionDeltaIdentityWithoutParent) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);
    ASSERT_NE(layerId, 0u);
    float outX = 0, outY = 0;
    ASSERT_TRUE(ms_layer_map_composition_delta(document, compositionId, layerId, 0.0,
                                               10.0f, -20.0f, &outX, &outY));
    EXPECT_FLOAT_EQ(outX, 10.0f);
    EXPECT_FLOAT_EQ(outY, -20.0f);
    ms_document_destroy(document);
}

TEST(BridgeCompositionTest, MapCompositionDeltaAccountsForRotatedParent) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t parentId = ms_command_add_rect_layer(document, compositionId);
    const uint64_t childId = ms_command_add_rect_layer(document, compositionId);
    ASSERT_NE(parentId, 0u);
    ASSERT_NE(childId, 0u);
    // Parent rotation 90° CCW at playhead 0 (static).
    ms_command_set_static_float(document, parentId, "transform.rotation", 90.0f);
    ASSERT_TRUE(ms_command_set_layer_parent(document, childId, parentId)); // 若 API 名不同，改用项目现有 setParent 命令
    float outX = 0, outY = 0;
    // Composition delta (10,0) under parent R=90° → parent space ≈ (0,-10)
    ASSERT_TRUE(ms_layer_map_composition_delta(document, compositionId, childId, 0.0,
                                               10.0f, 0.0f, &outX, &outY));
    EXPECT_NEAR(outX, 0.0f, 1e-3f);
    EXPECT_NEAR(outY, -10.0f, 1e-3f);
    ms_document_destroy(document);
}
```

**注意：** 实现前先在 bridge 头文件/命令里确认「设置父级」的真实 API 名（如 `ms_command_set_layer_parent` / 现有等价物）；若无公开命令，测试可改为直接通过已有 undo 命令或跳过父级用例、仅测无父级 + 单元测 Mat3（优先找到现有 setParent bridge）。

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target bridge_test
./build/bridge/bridge_test --gtest_filter='*MapCompositionDelta*'
```

Expected: 链接/编译失败。

- [ ] **Step 3: Declare + implement**

头文件：

```c
// Maps composition-space translation (dx,dy) into the layer's parent space
// using the inverse of the parent world linear transform. Identity when no parent.
bool ms_layer_map_composition_delta(MSDocument *document, uint64_t compositionId,
                                    uint64_t layerId, double frameTime, float dx, float dy,
                                    float *outParentDx, float *outParentDy);
```

实现要点：

```cpp
const Layer *layer = FindLayer(...);
Mat3 parentWorld = Mat3::Identity();
if (layer->parentId.isValid()) {
    const Layer *parent = doc->entityIndex().findLayer(layer->parentId);
    if (parent == nullptr) return false;
    parentWorld = parent->worldTransform(static_cast<FrameTime>(llround(frameTime)), *doc);
}
Mat3 inverse;
if (!parentWorld.tryInvert(inverse)) return false;
const Vec2 mapped = inverse.transformVector(Vec2{dx, dy});
*outParentDx = mapped.x;
*outParentDy = mapped.y;
return true;
```

`compositionId` 可用于校验层属于该合成（可选）；缺失则 false。

- [ ] **Step 4: Run tests to verify they pass**

同 Step 2。Expected: PASS。

- [ ] **Step 5: Update this plan** — Task 2 `[x]` + Status Done。

- [ ] **Step 6: Commit**

```bash
git commit --only bridge/include/motionstudio_bridge.h \
  bridge/src/common/motionstudio_bridge_composition.cpp \
  bridge/tests/BridgeTest.cpp \
  docs/superpowers/plans/2026-08-03-layer-align.md \
  -m "Map composition-space align deltas into layer parent space."
```

---

### Task 3: `MotionDocumentCore.alignLayers`

**Status:** ⏳

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift`

**Interfaces:**
- Consumes: `LayerAlign`、`layerBounds`、`compositionWidth/Height`、`ms_layer_map_composition_delta`、`evaluateVec2` / `writeVec2`（或 setStatic/addKeyframe 规则）、`TransformProperty.position`
- Produces:
  - `func mapCompositionDelta(compositionID:layerID:frame:delta:) -> CGVector?`
  - `func alignLayers(compositionID:layerIDs:edge:frame:)`

- [ ] **Step 1: Add methods**

```swift
func mapCompositionDelta(compositionID: UInt64, layerID: UInt64, frame: Int64,
                         delta: CGVector) -> CGVector?
{
    var outX: Float = 0
    var outY: Float = 0
    guard ms_layer_map_composition_delta(handle, compositionID, layerID, Double(frame),
                                         Float(delta.dx), Float(delta.dy), &outX, &outY)
    else { return nil }
    return CGVector(dx: CGFloat(outX), dy: CGFloat(outY))
}

func alignLayers(compositionID: UInt64, layerIDs: [UInt64], edge: LayerAlignEdge, frame: Int64) {
    guard !layerIDs.isEmpty else { return }
    let target: CGRect
    if layerIDs.count == 1 {
        let width = CGFloat(compositionWidth(compositionID))
        let height = CGFloat(compositionHeight(compositionID))
        target = CGRect(x: 0, y: 0, width: width, height: height)
    } else {
        let rects = layerIDs.compactMap {
            layerBounds(compositionID: compositionID, layerID: $0, frameTime: Double(frame))
        }
        guard let union = LayerAlign.unionBounds(rects) else { return }
        target = union
    }
    let path = TransformProperty.position.path
    for layerID in layerIDs {
        guard let bounds = layerBounds(compositionID: compositionID, layerID: layerID,
                                       frameTime: Double(frame)) else { continue }
        let deltaComp = LayerAlign.compositionDelta(edge: edge, bounds: bounds, target: target)
        if abs(deltaComp.dx) < 1e-6, abs(deltaComp.dy) < 1e-6 { continue }
        guard let deltaParent = mapCompositionDelta(compositionID: compositionID,
                                                    layerID: layerID,
                                                    frame: frame,
                                                    delta: deltaComp) else { continue }
        let stored = evaluateVec2(entityID: layerID, path: path, frame: frame)
        let next = CGVector(dx: stored.dx + deltaParent.dx, dy: stored.dy + deltaParent.dy)
        // Same rule as TransformInspector: keyframe at playhead → upsert else static
        if keyframes(entityID: layerID, path: path).contains(where: { $0.frame == frame }) {
            addKeyframeVec2(entityID: layerID, path: path, frame: frame, value: next)
        } else {
            setStaticVec2(entityID: layerID, path: path, value: next)
        }
    }
}
```

确认 `compositionWidth` / `compositionHeight` 现有命名；若不同则改用项目已有查询。

**本方法不调用 merge group**——由 UI 调用方包一层。

- [ ] **Step 2: Build**（Xcode MCP `BuildProject`）Expected: SUCCEEDED。

- [ ] **Step 3: Update this plan** — Task 3 Done。

- [ ] **Step 4: Commit**

```bash
git commit --only apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift \
  docs/superpowers/plans/2026-08-03-layer-align.md \
  -m "Add alignLayers on MotionDocumentCore with parent-space writes."
```

---

### Task 4: Toolbar UI（Inspector 前）+ 选中显隐

**Status:** ⏳

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController.swift`（按钮属性）
- Modify: `apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController+Layout.swift`（布局）
- Modify: `apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController+Commands.swift`（actions）
- 可能：`EditorViewController+Layout.swift` 或 observation 处已有 `selectedLayerIDs` 刷新 → 调用 `updateAlignToolbarVisibility()`

**Interfaces:**
- Consumes: `alignLayers`、`editorState.selectedLayerIDs`、`perform`、`beginMergeGroup`/`endMergeGroup`、`PlayheadClock`/当前帧
- Produces: 6 按钮组；无选中 `isHidden = true`

- [ ] **Step 1: Add six buttons + stack**

在 `EditorViewController` 增加 6 个 `UIButton`（或一个 `alignStack` 内含 6 按钮）。SF Symbol 建议：

| Edge | Symbol | Label |
|---|---|---|
| left | `align.horizontal.left` | Align Left |
| horizontalCenter | `align.horizontal.center` | Align Horizontal Centers |
| right | `align.horizontal.right` | Align Right |
| top | `align.vertical.top` | Align Top |
| verticalCenter | `align.vertical.center` | Align Vertical Centers |
| bottom | `align.vertical.bottom` | Align Bottom |

（若系统版本缺符号，用项目已有 icon 风格或 `text.align*` 替代，保持 6 键语义。）

- [ ] **Step 2: Insert before Inspector in `configureTopToolbar`**

将：

```swift
contentStack.addArrangedSubview(UIView())
contentStack.addArrangedSubview(inspectorToggleButton)
```

改为：

```swift
contentStack.addArrangedSubview(UIView()) // spacer
contentStack.addArrangedSubview(alignToolbarStack) // 内含 6 按钮，spacing 小
contentStack.addArrangedSubview(inspectorToggleButton)
```

`configureToolbarButton` 复用现有样式。

- [ ] **Step 3: Actions + merge undo**

```swift
@objc func alignSelectionLeft() { alignSelection(edge: .left, name: "Align Left") }
// … 其余 5 个同理

func alignSelection(edge: LayerAlignEdge, name: String) {
    let layerIDs = editorState.selectedLayerIDs
    guard !layerIDs.isEmpty else { return }
    let compositionID = document.core.firstCompositionID
    let frame = /* 当前 playhead Int64，与 Inspector 同源 */
    perform(name) {
        document.core.beginMergeGroup()
        document.core.alignLayers(compositionID: compositionID,
                                  layerIDs: layerIDs,
                                  edge: edge,
                                  frame: frame)
        document.core.endMergeGroup()
    }
}

func updateAlignToolbarVisibility() {
    alignToolbarStack.isHidden = editorState.selectedLayerIDs.isEmpty
}
```

在已有观察 `selectedLayerIDs` 的刷新路径调用 `updateAlignToolbarVisibility()`（见 `EditorViewController+Layout.swift` 约 351 行附近）。

- [ ] **Step 4: Build + 手动点一次对齐**（MCP Build）。Expected: SUCCEEDED。

- [ ] **Step 5: Update this plan** — Task 4 Done。

- [ ] **Step 6: Commit**

```bash
git commit --only \
  apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController.swift \
  apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController+Layout.swift \
  apps/MotionStudioApp/MotionStudioApp/Editor/EditorViewController+Commands.swift \
  docs/superpowers/plans/2026-08-03-layer-align.md \
  -m "Add layer align controls before the inspector toolbar button."
```

---

### Task 5: Spec 状态 + 验收

**Status:** ⏳

**Files:**
- Modify: `docs/superpowers/specs/2026-08-03-layer-align-design.md`
- Modify: `docs/superpowers/plans/2026-08-03-layer-align.md`

- [ ] **Step 1: Spec 状态改为** `已实现（待人机验收）`

- [ ] **Step 2: Manual checklist**

1. 单选 → Align Left：视觉左边贴 x=0  
2. 两层 → Align Horizontal Centers：中线对齐并集  
3. 锁定层被选中时仍移动  
4. 多选对齐后 **一次 Undo** 全部还原  
5. 无选中时 Align 组隐藏；有选中出现在 Inspector 左侧  

- [ ] **Step 3: Update this plan** — Task 5 Step1/3 `[x]`；Step2 人机通过后勾选。

- [ ] **Step 4: Commit**

```bash
git commit --only \
  docs/superpowers/specs/2026-08-03-layer-align-design.md \
  docs/superpowers/plans/2026-08-03-layer-align.md \
  -m "Mark layer align spec implemented pending acceptance."
```

---

## Spec Coverage Self-Review

| Spec | Task |
|---|---|
| Toolbar Inspector 前 / 无选中隐藏 | Task 4 |
| 单选合成 / 多选并集 | Task 3 |
| 视觉 AABB | Task 3 `layerBounds` |
| 6 向 only | Task 1 + 4 |
| 锁定隐藏仍移动 | Task 3 不过滤 lock/visible |
| merge undo | Task 4 `begin/endMergeGroup` + `perform` |
| 父空间换算 | Task 2 |
| 纯函数测试 | Task 1 |

无 TBD。Task 2 实现时须核对 setParent 的真实 Bridge API 名后再写父级旋转测试。
