# Anchor Preset Grid Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在单选图层的 Transform Inspector 中提供示意线框九点锚点快捷设置，点击后设局部锚点并补偿 position，画面不跳。

**Architecture:** Bridge 新增 `ms_layer_local_bounds`（EvaluatePreview → ResolvePointTextContainerSizes → BoundsOfLayerLocal）。Swift 侧 `AnchorPreset` 做九点坐标 / 容差匹配 / position 补偿；`AnchorPresetFrame` 画线框可点击点；`TransformInspector` 接线写入。

**Tech Stack:** C++17 bridge、GoogleTest、`MotionDocumentCore`、SwiftUI、Swift Testing。

**Spec:** `docs/superpowers/specs/2026-08-01-anchor-preset-grid-design.md`

## Global Constraints

- 仅单选；无 local bounds 时隐藏九点，保留 Anchor X/Y 数值行。
- Bounds 必须走 `ms_layer_local_bounds`，禁止在 Swift 拼装 Image/Text/Shape 尺寸。
- 点击补偿 position，公式与 `FreeTransformDrag.applyAnchor` 一致：`Δscene = rotate(scale(Δlocal))`。
- 不新增 Core undo command 类型；复用 `setStaticVec2` / `addKeyframeVec2`。
- 提交：每任务结束后 commit（不推送）；commit 信息英语一句、句号结尾、无其它标点。
- 新 Swift 文件放在 `apps/MotionStudioApp/MotionStudioApp/Inspector/`（folder 引用，无需改 pbxproj）。

## File Map

| 区域 | 文件 |
|---|---|
| Bridge API | `bridge/include/motionstudio_bridge.h`、`bridge/src/common/motionstudio_bridge_composition.cpp` |
| Bridge 测试 | `bridge/tests/BridgeTest.cpp` |
| Core 依赖（只读复用） | `BoundsOfLayerLocal` in `src/render/HitTest.cpp`、`ResolvePointTextContainerSizes` |
| Swift 封装 | `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift` |
| 纯逻辑 | `apps/MotionStudioApp/MotionStudioApp/Inspector/AnchorPreset.swift` |
| UI | `apps/MotionStudioApp/MotionStudioApp/Inspector/AnchorPresetFrame.swift` |
| 接线 | `TransformInspector.swift`、`InspectorView.swift` |
| Swift 测试 | `apps/MotionStudioApp/MotionStudioAppTests/AnchorPresetTests.swift` |
| Spec 状态 | `docs/superpowers/specs/2026-08-01-anchor-preset-grid-design.md` |

---

### Task 1: Bridge `ms_layer_local_bounds` + tests

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`（紧挨 `ms_composition_layer_bounds` 声明之后）
- Modify: `bridge/src/common/motionstudio_bridge_composition.cpp`
- Modify: `bridge/tests/BridgeTest.cpp`
- Test: `bridge/tests/BridgeTest.cpp`

**Interfaces:**
- Consumes: `SceneEvaluator::EvaluatePreview`、`ResolvePointTextContainerSizes`、`BoundsOfLayerLocal`
- Produces: `bool ms_layer_local_bounds(MSDocument*, uint64_t compositionId, uint64_t layerId, double frameTime, float* minX, float* minY, float* maxX, float* maxY)`

- [ ] **Step 1: Write the failing test**

在 `BridgeTest.cpp` 追加（可放在 `TextLayerAddSetStringFontAndUndo` 附近）：

```cpp
TEST(BridgeCompositionTest, LayerLocalBoundsMatchesSelectionHandlesAndPointText) {
    MSDocument *document = ms_document_create();
    ASSERT_NE(document, nullptr);
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);

    // Rect shape: local bounds must match selection_handles localMin/Max.
    const uint64_t rectId = ms_command_add_rect_layer(document, compositionId);
    ASSERT_NE(rectId, 0u);
    float minX = 0, minY = 0, maxX = 0, maxY = 0;
    ASSERT_TRUE(ms_layer_local_bounds(document, compositionId, rectId, 0.0, &minX, &minY, &maxX, &maxY));
    MSSelectionHandles handles = {};
    const uint64_t ids[] = {rectId};
    ASSERT_TRUE(ms_composition_selection_handles(document, compositionId, 0.0, ids, 1, rectId, &handles));
    EXPECT_FLOAT_EQ(minX, handles.localMinX);
    EXPECT_FLOAT_EQ(minY, handles.localMinY);
    EXPECT_FLOAT_EQ(maxX, handles.localMaxX);
    EXPECT_FLOAT_EQ(maxY, handles.localMaxY);
    EXPECT_GT(maxX - minX, 0.0f);
    EXPECT_GT(maxY - minY, 0.0f);

    // Point text: local size is measured glyphs, not placeholder 400×120.
    const uint64_t textId = ms_command_add_text_layer(document, compositionId);
    ASSERT_NE(textId, 0u);
    EXPECT_FALSE(ms_layer_text_box_text_mode(document, textId));
    ASSERT_TRUE(ms_layer_local_bounds(document, compositionId, textId, 0.0, &minX, &minY, &maxX, &maxY));
    EXPECT_FLOAT_EQ(minX, 0.0f);
    EXPECT_FLOAT_EQ(minY, 0.0f);
    EXPECT_GT(maxX, 0.0f);
    EXPECT_GT(maxY, 0.0f);
    EXPECT_LT(maxX, 400.0f);
    EXPECT_LT(maxY, 120.0f);

    // Box text: local bounds follow content.size after mode switch.
    ASSERT_TRUE(ms_command_set_text_box_text_mode(document, textId, true, 0));
    ASSERT_TRUE(ms_command_set_text_size(document, textId, 300.0f, 100.0f));
    ASSERT_TRUE(ms_layer_local_bounds(document, compositionId, textId, 0.0, &minX, &minY, &maxX, &maxY));
    EXPECT_FLOAT_EQ(minX, 0.0f);
    EXPECT_FLOAT_EQ(minY, 0.0f);
    EXPECT_FLOAT_EQ(maxX, 300.0f);
    EXPECT_FLOAT_EQ(maxY, 100.0f);

    // Missing layer → false.
    EXPECT_FALSE(ms_layer_local_bounds(document, compositionId, 0, 0.0, &minX, &minY, &maxX, &maxY));

    ms_document_destroy(document);
}
```

若工程里尚无 `BridgeCompositionTest` suite 名，用现有 suite 前缀（如 `BridgeCommandTest`）亦可，保持文件风格一致即可。

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target bridge_test
./build/bridge/tests/bridge_test --gtest_filter='*LayerLocalBounds*'
```

Expected: 链接/编译失败（符号不存在）或测试未注册。

- [ ] **Step 3: Declare + implement API**

在 `motionstudio_bridge.h` 于 `ms_composition_layer_bounds` 后插入：

```c
// Layer-local AABB (same source as selection handles localMin/localMax).
// Runs EvaluatePreview → ResolvePointTextContainerSizes → BoundsOfLayerLocal.
// Returns false when the layer is missing or has no local bounds.
bool ms_layer_local_bounds(MSDocument *document, uint64_t compositionId, uint64_t layerId, double frameTime,
                           float *minX, float *minY, float *maxX, float *maxY);
```

在 `motionstudio_bridge_composition.cpp` 实现（镜像 `ms_composition_layer_bounds`，但调用 `BoundsOfLayerLocal`）：

```cpp
bool ms_layer_local_bounds(MSDocument *document, uint64_t compositionId, uint64_t layerId, double frameTime,
                           float *minX, float *minY, float *maxX, float *maxY) {
    DocumentLock guard(document);
    Document *doc = Doc(document);
    if (doc == nullptr) {
        return false;
    }
    auto result = SceneEvaluator::EvaluatePreview(*doc, EntityId{compositionId}, motion::PreviewTime(frameTime));
    if (!result.hasValue()) {
        return false;
    }
    motion::SceneState &state = result.value();
    ResolvePointTextContainerSizes(state);
    for (const motion::EvaluatedLayer &layer : state.layers) {
        if (layer.id.value != layerId) {
            continue;
        }
        Vec2 minPoint;
        Vec2 maxPoint;
        if (!motion::BoundsOfLayerLocal(layer, minPoint, maxPoint)) {
            return false;
        }
        if (minX != nullptr) {
            *minX = minPoint.x;
        }
        if (minY != nullptr) {
            *minY = minPoint.y;
        }
        if (maxX != nullptr) {
            *maxX = maxPoint.x;
        }
        if (maxY != nullptr) {
            *maxY = maxPoint.y;
        }
        return true;
    }
    return false;
}
```

确认已 `#include "MotionStudio/render/HitTest.h"`（若文件尚未 include）。

- [ ] **Step 4: Run test to verify it passes**

```bash
cmake --build build --target bridge_test
./build/bridge/tests/bridge_test --gtest_filter='*LayerLocalBounds*'
```

Expected: PASS。

- [ ] **Step 5: Commit**

```bash
git add bridge/include/motionstudio_bridge.h \
  bridge/src/common/motionstudio_bridge_composition.cpp \
  bridge/tests/BridgeTest.cpp
git commit -m "Add bridge API for layer-local content bounds."
```

---

### Task 2: Swift `AnchorPreset` + unit tests

**Files:**
- Create: `apps/MotionStudioApp/MotionStudioApp/Inspector/AnchorPreset.swift`
- Create: `apps/MotionStudioApp/MotionStudioAppTests/AnchorPresetTests.swift`

**Interfaces:**
- Consumes: 无（纯 Foundation/CoreGraphics）
- Produces:
  - `enum AnchorPresetCorner: CaseIterable` — 九个 case
  - `enum AnchorPreset` with:
    - `static func point(corner:in:) -> CGVector`
    - `static func matchingCorner(anchor:rect:tolerance:) -> AnchorPresetCorner?`
    - `static func compensatedPosition(oldAnchor:newAnchor:position:scale:rotationDegrees:) -> CGVector`

- [ ] **Step 1: Write the failing tests**

`AnchorPresetTests.swift`：

```swift
import CoreGraphics
import Foundation
@testable import MotionStudio
import Testing

struct AnchorPresetTests {
    @Test
    func `nine points cover rect corners edges and center`() {
        let rect = CGRect(x: 10, y: 20, width: 100, height: 40)
        #expect(AnchorPreset.point(corner: .topLeft, in: rect) == CGVector(dx: 10, dy: 20))
        #expect(AnchorPreset.point(corner: .topCenter, in: rect) == CGVector(dx: 60, dy: 20))
        #expect(AnchorPreset.point(corner: .topRight, in: rect) == CGVector(dx: 110, dy: 20))
        #expect(AnchorPreset.point(corner: .middleLeft, in: rect) == CGVector(dx: 10, dy: 40))
        #expect(AnchorPreset.point(corner: .center, in: rect) == CGVector(dx: 60, dy: 40))
        #expect(AnchorPreset.point(corner: .middleRight, in: rect) == CGVector(dx: 110, dy: 40))
        #expect(AnchorPreset.point(corner: .bottomLeft, in: rect) == CGVector(dx: 10, dy: 60))
        #expect(AnchorPreset.point(corner: .bottomCenter, in: rect) == CGVector(dx: 60, dy: 60))
        #expect(AnchorPreset.point(corner: .bottomRight, in: rect) == CGVector(dx: 110, dy: 60))
    }

    @Test
    func `matching corner uses tolerance`() {
        let rect = CGRect(x: 0, y: 0, width: 200, height: 100)
        #expect(AnchorPreset.matchingCorner(anchor: CGVector(dx: 100.2, dy: 50.1),
                                            rect: rect,
                                            tolerance: 0.5) == .center)
        #expect(AnchorPreset.matchingCorner(anchor: CGVector(dx: 30, dy: 30),
                                            rect: rect,
                                            tolerance: 0.5) == nil)
    }

    @Test
    func `compensated position keeps content fixed under rotation and scale`() {
        // oldAnchor (0,0) → newAnchor (100,0); scale (2,1); rotation 90° CCW
        // Δlocal=(100,0) → scaled=(200,0) → rotated=(0,200)
        let newPosition = AnchorPreset.compensatedPosition(
            oldAnchor: CGVector(dx: 0, dy: 0),
            newAnchor: CGVector(dx: 100, dy: 0),
            position: CGVector(dx: 50, dy: 50),
            scale: CGVector(dx: 2, dy: 1),
            rotationDegrees: 90,
        )
        #expect(abs(newPosition.dx - 50) < 1e-4)
        #expect(abs(newPosition.dy - 250) < 1e-4)
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

优先 Xcode MCP `BuildProject` / 跑 `MotionStudioAppTests`；不可用则：

```bash
xcodebuild -workspace MotionStudio.xcworkspace -scheme MotionStudioApp \
  -destination 'platform=macOS,variant=Mac Catalyst' \
  -only-testing:MotionStudioAppTests/AnchorPresetTests test
```

Expected: 编译失败（`AnchorPreset` 未定义）。

- [ ] **Step 3: Implement `AnchorPreset.swift`**

```swift
import CoreGraphics
import Foundation

enum AnchorPresetCorner: CaseIterable {
    case topLeft, topCenter, topRight
    case middleLeft, center, middleRight
    case bottomLeft, bottomCenter, bottomRight
}

enum AnchorPreset {
    static func point(corner: AnchorPresetCorner, in rect: CGRect) -> CGVector {
        let x: CGFloat
        let y: CGFloat
        switch corner {
        case .topLeft, .middleLeft, .bottomLeft:
            x = rect.minX
        case .topCenter, .center, .bottomCenter:
            x = rect.midX
        case .topRight, .middleRight, .bottomRight:
            x = rect.maxX
        }
        switch corner {
        case .topLeft, .topCenter, .topRight:
            y = rect.minY
        case .middleLeft, .center, .middleRight:
            y = rect.midY
        case .bottomLeft, .bottomCenter, .bottomRight:
            y = rect.maxY
        }
        return CGVector(dx: x, dy: y)
    }

    static func matchingCorner(anchor: CGVector,
                               rect: CGRect,
                               tolerance: CGFloat = 0.5) -> AnchorPresetCorner?
    {
        for corner in AnchorPresetCorner.allCases {
            let preset = point(corner: corner, in: rect)
            if abs(preset.dx - anchor.dx) <= tolerance, abs(preset.dy - anchor.dy) <= tolerance {
                return corner
            }
        }
        return nil
    }

    static func compensatedPosition(oldAnchor: CGVector,
                                    newAnchor: CGVector,
                                    position: CGVector,
                                    scale: CGVector,
                                    rotationDegrees: Float) -> CGVector
    {
        let deltaLocal = CGPoint(x: newAnchor.dx - oldAnchor.dx, y: newAnchor.dy - oldAnchor.dy)
        let scaled = CGPoint(x: deltaLocal.x * scale.dx, y: deltaLocal.y * scale.dy)
        let radians = CGFloat(rotationDegrees) * .pi / 180
        let cosine = cos(radians)
        let sine = sin(radians)
        let rotated = CGPoint(x: scaled.x * cosine - scaled.y * sine,
                              y: scaled.x * sine + scaled.y * cosine)
        return CGVector(dx: position.dx + rotated.x, dy: position.dy + rotated.y)
    }
}
```

- [ ] **Step 4: Run tests to verify they pass**

同 Step 2 命令。Expected: PASS。

- [ ] **Step 5: Commit**

```bash
git add apps/MotionStudioApp/MotionStudioApp/Inspector/AnchorPreset.swift \
  apps/MotionStudioApp/MotionStudioAppTests/AnchorPresetTests.swift
git commit -m "Add AnchorPreset math for nine-point anchor shortcuts."
```

---

### Task 3: `AnchorPresetFrame` 示意线框 UI

**Files:**
- Create: `apps/MotionStudioApp/MotionStudioApp/Inspector/AnchorPresetFrame.swift`

**Interfaces:**
- Consumes: `AnchorPresetCorner`
- Produces: `struct AnchorPresetFrame: View` — `selected: AnchorPresetCorner?`、`isEnabled: Bool`、`onSelect: (AnchorPresetCorner) -> Void`

- [ ] **Step 1: Implement wireframe control**

固定约 44×44（或 48×48）示意矩形；九个点按相对位置布局；点可视半径 ~3pt，按钮命中 ~22×22；选中用 `Color.accentColor` 实心，未选中描边空心；`disabled(!isEnabled)`。

参考结构：

```swift
import SwiftUI

struct AnchorPresetFrame: View {
    let selected: AnchorPresetCorner?
    let isEnabled: Bool
    let onSelect: (AnchorPresetCorner) -> Void

    private let boxSize: CGFloat = 44
    private let dotRadius: CGFloat = 3

    var body: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 2)
                .strokeBorder(Color.secondary.opacity(0.55), lineWidth: 1)
                .frame(width: boxSize, height: boxSize)
            ForEach(AnchorPresetCorner.allCases, id: \.self) { corner in
                Button {
                    onSelect(corner)
                } label: {
                    Circle()
                        .strokeBorder(selected == corner ? Color.accentColor : Color.secondary, lineWidth: 1)
                        .background(
                            Circle().fill(selected == corner ? Color.accentColor : Color.clear),
                        )
                        .frame(width: dotRadius * 2, height: dotRadius * 2)
                        .frame(width: 22, height: 22)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .position(dotPosition(corner))
            }
        }
        .frame(width: boxSize + 12, height: boxSize + 12)
        .disabled(!isEnabled)
        .opacity(isEnabled ? 1 : 0.45)
        .accessibilityLabel("Anchor preset")
    }

    private func dotPosition(_ corner: AnchorPresetCorner) -> CGPoint {
        let inset: CGFloat = 6
        let origin = CGPoint(x: inset, y: inset)
        let size = boxSize
        let x: CGFloat
        let y: CGFloat
        switch corner {
        case .topLeft, .middleLeft, .bottomLeft: x = origin.x
        case .topCenter, .center, .bottomCenter: x = origin.x + size / 2
        case .topRight, .middleRight, .bottomRight: x = origin.x + size
        }
        switch corner {
        case .topLeft, .topCenter, .topRight: y = origin.y
        case .middleLeft, .center, .middleRight: y = origin.y + size / 2
        case .bottomLeft, .bottomCenter, .bottomRight: y = origin.y + size
        }
        return CGPoint(x: x, y: y)
    }
}

extension AnchorPresetCorner: Hashable {}
```

若 `CaseIterable` 已满足 `ForEach` 的 `Identifiable` 需求，可用 `id: \.self`（需 `Hashable`）。按项目现有 SwiftUI 风格微调间距/颜色，勿引入新设计系统。

- [ ] **Step 2: Build app target to verify compile**

优先 Xcode MCP `BuildProject`；回退：

```bash
xcodebuild -workspace MotionStudio.xcworkspace -scheme MotionStudioApp \
  -configuration Debug \
  -destination "generic/platform=macOS,variant=Mac Catalyst,name=Any Mac" \
  ARCHS="arm64" build
```

Expected: BUILD SUCCEEDED。

- [ ] **Step 3: Commit**

```bash
git add apps/MotionStudioApp/MotionStudioApp/Inspector/AnchorPresetFrame.swift
git commit -m "Add wireframe nine-point anchor preset control."
```

---

### Task 4: Wire Inspector + `layerLocalBounds`

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift`（`layerBounds` 附近）
- Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/TransformInspector.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/InspectorView.swift`
- Modify: `docs/superpowers/specs/2026-08-01-anchor-preset-grid-design.md`（状态行指向本 plan）

**Interfaces:**
- Consumes: `ms_layer_local_bounds`、`AnchorPreset`、`AnchorPresetFrame`
- Produces: `MotionDocumentCore.layerLocalBounds(compositionID:layerID:frameTime:) -> CGRect?`；`TransformInspector(compositionID:)`

- [ ] **Step 1: Add Core wrapper**

在 `MotionDocumentCore` 的 `layerBounds` 旁：

```swift
func layerLocalBounds(compositionID: UInt64, layerID: UInt64, frameTime: Double) -> CGRect? {
    var minX: Float = 0
    var minY: Float = 0
    var maxX: Float = 0
    var maxY: Float = 0
    guard ms_layer_local_bounds(handle, compositionID, layerID, frameTime,
                                &minX, &minY, &maxX, &maxY)
    else {
        return nil
    }
    return CGRect(x: CGFloat(minX),
                  y: CGFloat(minY),
                  width: CGFloat(maxX - minX),
                  height: CGFloat(maxY - minY))
}
```

先 `apps/gen_mac`（或等价）确保 bridging header / 静态库含新符号，再编 App。

- [ ] **Step 2: Update `InspectorView` → pass `compositionID`**

```swift
TransformInspector(core: core,
                   compositionID: core.firstCompositionID,
                   layerID: layerID,
                   isEditable: isEditable,
                   perform: perform)
```

（与 `FollowPathInspector` 一致。）

- [ ] **Step 3: Wire `TransformInspector`**

在 `TransformInspector` 增加 `compositionID: UInt64`。在 Anchor X/Y **之前**：

```swift
let frameTime = Double(playheadFrame) // 若项目 evaluation 用秒，改用与 canvas 相同的求值时间
if let localBounds = core.layerLocalBounds(compositionID: compositionID,
                                           layerID: layerID,
                                           frameTime: frameTime),
   localBounds.width > 0, localBounds.height > 0
{
    let selectedCorner = AnchorPreset.matchingCorner(anchor: anchor, rect: localBounds)
    AnchorPresetFrame(selected: selectedCorner, isEnabled: isEditable) { corner in
        applyAnchorPreset(corner, localBounds: localBounds)
    }
}
```

`applyAnchorPreset`：

```swift
private func applyAnchorPreset(_ corner: AnchorPresetCorner, localBounds: CGRect) {
    guard isEditable else { return }
    let oldAnchor = core.evaluateVec2(entityID: layerID,
                                      path: TransformProperty.anchorPoint.path,
                                      frame: playheadFrame)
    let newAnchor = AnchorPreset.point(corner: corner, in: localBounds)
    if let match = AnchorPreset.matchingCorner(anchor: oldAnchor, rect: localBounds),
       match == corner
    {
        return
    }
    let oldPosition = core.evaluateVec2(entityID: layerID,
                                        path: TransformProperty.position.path,
                                        frame: playheadFrame)
    let scale = core.evaluateVec2(entityID: layerID,
                                  path: TransformProperty.scale.path,
                                  frame: playheadFrame)
    let rotation = core.evaluateFloat(entityID: layerID,
                                      path: TransformProperty.rotation.path,
                                      frame: playheadFrame)
    let newPosition = AnchorPreset.compensatedPosition(oldAnchor: oldAnchor,
                                                       newAnchor: newAnchor,
                                                       position: oldPosition,
                                                       scale: scale,
                                                       rotationDegrees: rotation)
    perform("Set Anchor") {
        setVec2Property(.anchorPoint, value: newAnchor)
        // setVec2Property 内部各走 performSet；应改为单次 perform 内直接写两次：
        writeVec2(.anchorPoint, value: newAnchor)
        writeVec2(.position, value: newPosition)
    }
}
```

注意：现有 `setVec2Property` 会再包一层 `perform(...)`，会导致两个 undo 单元。实现时抽出私有 `writeVec2`（复制 `setVec2Property` 的 keyframe/static 分支、**不含** `perform`），在一个 `perform("Set Anchor")` 里连续调用；或 `beginDrag`/`endDrag` 合并——优先**单次 `perform` + 无嵌套 perform 的 write**，与「一个 undo 单元」一致。

`frameTime` 必须与画布 selection handles 使用同一时间基。查看 `CanvasViewController.evaluationFrame` / `PlayheadClock`：若 canvas 传 `Double(frame)` 给 bridge，Inspector 同样用 `Double(playheadFrame)`；若用秒，两边对齐。

- [ ] **Step 4: Build + smoke**

```bash
# 重建 bridge/core 产物后编 App
apps/gen_mac   # 若符号未进 Products
# 然后 Xcode MCP BuildProject 或 xcodebuild（同 Task 3）
```

手动：单选矩形 → 点四角/中心，画面不跳、Anchor/Position 数值变化、对应点高亮；点文本高亮基于测量框；锁定图层控件禁用。

- [ ] **Step 5: Update spec status + commit**

Spec 状态改为：`已确认；实现计划见 docs/superpowers/plans/2026-08-01-anchor-preset-grid.md`

```bash
git add apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift \
  apps/MotionStudioApp/MotionStudioApp/Inspector/TransformInspector.swift \
  apps/MotionStudioApp/MotionStudioApp/Inspector/InspectorView.swift \
  docs/superpowers/specs/2026-08-01-anchor-preset-grid-design.md
git commit -m "Wire nine-point anchor presets into the transform inspector."
```

---

## Spec Coverage Checklist

| Spec 要求 | Task |
|---|---|
| 示意线框 + 可点击九点 | Task 3 |
| Anchor X/Y 上方 | Task 4 |
| 补偿 position | Task 2 + 4 |
| 容差高亮 | Task 2 + 4 |
| `ms_layer_local_bounds` | Task 1 |
| 点文本测量尺寸 | Task 1（ResolvePointTextContainerSizes） |
| 单选 / 无 bounds 隐藏 | Task 4 |
| 一个 undo 单元 | Task 4（单次 perform + writeVec2） |
| Swift 纯函数测试 | Task 2 |
| 不改画布拖锚点 / 不删数值行 | 全任务未触及 |

## Self-Review Notes

- 无 TBD/占位步骤。
- `compensatedPosition` 签名在 Task 2/4 一致。
- API 名 `ms_layer_local_bounds` 在 header / 实现 / Swift / 测试一致。
- Task 4 明确避免嵌套 `perform` 拆成两个 undo。
