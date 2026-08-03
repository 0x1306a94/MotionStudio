# Layout Position UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让所有向用户展示/编辑的 `transform.position` 数字表示局部 AABB 左上角在父空间中的位置，底层仍存 AE 式锚点位置。

**Architecture:** Swift 纯函数 `LayoutPosition` 负责 `offset = R·S·(bounds.min − anchor)` 与 stored↔layout 换算；`MotionDocumentCore` 提供 `evaluateLayoutPosition` / `writeLayoutPosition` 门面（读 `layerLocalBounds` + transform 属性）；Inspector / Motion Path 数值入口改走门面。FreeTransform 等内部写回保持存储坐标。

**Tech Stack:** SwiftUI、`MotionDocumentCore`、Swift Testing、现有 `ms_layer_local_bounds`。

**Spec:** `docs/superpowers/specs/2026-08-03-layout-position-ui-design.md`

## Global Constraints

- 不改 Core `Transform`、文件格式、序列化、Lottie/PAG 导出。
- 不改 `ShapeProperty.position`；不改 FreeTransform / 锚点补偿 / recenter 的存储坐标写回。
- 左上角 = `localBounds.min`；无 bounds 或空 rect → `offset = 0`。
- 旋转缩放公式与 `AnchorPreset.compensatedPosition` 一致：先 scale 分量乘，再按度转弧度旋转。
- 新 Swift 文件放 `apps/MotionStudioApp/MotionStudioApp/Inspector/`（folder 引用，无需改 pbxproj）；测试放 `MotionStudioAppTests/`。
- 提交：每任务结束后 commit（不推送）；commit 信息英语一句、句号结尾、无其它标点。

## File Map

| 区域 | 文件 |
|---|---|
| 纯逻辑 | Create: `apps/MotionStudioApp/MotionStudioApp/Inspector/LayoutPosition.swift` |
| 纯逻辑测试 | Create: `apps/MotionStudioApp/MotionStudioAppTests/LayoutPositionTests.swift` |
| Core 门面 | Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift` |
| Transform UI | Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/TransformInspector.swift` |
| Motion Path UI | Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/MotionPathInspector.swift`、`InspectorView.swift` |
| Spec 状态 | Modify: `docs/superpowers/specs/2026-08-03-layout-position-ui-design.md` |

---

### Task 1: `LayoutPosition` 纯函数 + 单元测试

**Files:**
- Create: `apps/MotionStudioApp/MotionStudioApp/Inspector/LayoutPosition.swift`
- Create: `apps/MotionStudioApp/MotionStudioAppTests/LayoutPositionTests.swift`
- Test: `apps/MotionStudioApp/MotionStudioAppTests/LayoutPositionTests.swift`

**Interfaces:**
- Consumes: 无（纯 `CoreGraphics`）
- Produces:
  - `enum LayoutPosition`
  - `static func offset(anchor: CGVector, scale: CGVector, rotationDegrees: Float, localBounds: CGRect) -> CGVector`
  - `static func toLayout(stored: CGVector, offset: CGVector) -> CGVector`
  - `static func toStored(layout: CGVector, offset: CGVector) -> CGVector`

- [ ] **Step 1: Write the failing tests**

```swift
//
//  LayoutPositionTests.swift
//  MotionStudioAppTests
//

import CoreGraphics
import Foundation
@testable import MotionStudio
import Testing

@MainActor
struct LayoutPositionTests {
    @Test
    func `image center anchor maps layout to top left`() {
        let bounds = CGRect(x: 0, y: 0, width: 200, height: 200)
        let offset = LayoutPosition.offset(anchor: CGVector(dx: 100, dy: 100),
                                           scale: CGVector(dx: 1, dy: 1),
                                           rotationDegrees: 0,
                                           localBounds: bounds)
        #expect(abs(offset.dx - 100) < 1e-4)
        #expect(abs(offset.dy - 100) < 1e-4)
        let layout = LayoutPosition.toLayout(stored: CGVector(dx: 200, dy: 200), offset: offset)
        #expect(abs(layout.dx - 100) < 1e-4)
        #expect(abs(layout.dy - 100) < 1e-4)
    }

    @Test
    func `recentered shape min corner offset`() {
        let bounds = CGRect(x: -50, y: -40, width: 100, height: 80)
        let offset = LayoutPosition.offset(anchor: CGVector(dx: 0, dy: 0),
                                           scale: CGVector(dx: 1, dy: 1),
                                           rotationDegrees: 0,
                                           localBounds: bounds)
        #expect(abs(offset.dx - (-50)) < 1e-4)
        #expect(abs(offset.dy - (-40)) < 1e-4)
        let layout = LayoutPosition.toLayout(stored: CGVector(dx: 300, dy: 300), offset: offset)
        #expect(abs(layout.dx - 350) < 1e-4)
        #expect(abs(layout.dy - 340) < 1e-4)
    }

    @Test
    func `write layout zero stores position at offset`() {
        let offset = CGVector(dx: 100, dy: 50)
        let stored = LayoutPosition.toStored(layout: .zero, offset: offset)
        #expect(abs(stored.dx - 100) < 1e-4)
        #expect(abs(stored.dy - 50) < 1e-4)
        let roundTrip = LayoutPosition.toLayout(stored: stored, offset: offset)
        #expect(abs(roundTrip.dx) < 1e-4)
        #expect(abs(roundTrip.dy) < 1e-4)
    }

    @Test
    func `rotation and scale transform min minus anchor`() {
        // bounds.min=(0,0), anchor=(10,0), scale=(2,1), rot=90° CCW
        // delta=(-10,0) → scaled=(-20,0) → rotated=(0,-20)
        let bounds = CGRect(x: 0, y: 0, width: 40, height: 20)
        let offset = LayoutPosition.offset(anchor: CGVector(dx: 10, dy: 0),
                                           scale: CGVector(dx: 2, dy: 1),
                                           rotationDegrees: 90,
                                           localBounds: bounds)
        #expect(abs(offset.dx - 0) < 1e-4)
        #expect(abs(offset.dy - (-20)) < 1e-4)
    }

    @Test
    func `empty bounds yield zero offset`() {
        let offset = LayoutPosition.offset(anchor: CGVector(dx: 100, dy: 100),
                                           scale: CGVector(dx: 1, dy: 1),
                                           rotationDegrees: 0,
                                           localBounds: .null)
        #expect(offset == .zero)
        let empty = LayoutPosition.offset(anchor: CGVector(dx: 100, dy: 100),
                                          scale: CGVector(dx: 1, dy: 1),
                                          rotationDegrees: 0,
                                          localBounds: CGRect(x: 0, y: 0, width: 0, height: 10))
        #expect(empty == .zero)
    }
}
```

- [ ] **Step 2: Run tests to verify they fail**

优先 Xcode MCP；不可用则：

```bash
xcodebuild -workspace MotionStudio.xcworkspace -scheme MotionStudioApp \
  -destination 'platform=macOS,variant=Mac Catalyst' \
  -only-testing:MotionStudioAppTests/LayoutPositionTests test
```

Expected: 编译失败（`LayoutPosition` 未定义）。

- [ ] **Step 3: Implement `LayoutPosition.swift`**

```swift
//
//  LayoutPosition.swift
//  MotionStudioApp
//
//  UI presentation helpers: stored AE position (anchor in parent space)
//  ↔ layout position (local AABB top-left in parent space).
//

import CoreGraphics
import Foundation

enum LayoutPosition {
    /// offset = R · S · (bounds.min − anchor). Empty / null bounds → .zero.
    static func offset(anchor: CGVector,
                       scale: CGVector,
                       rotationDegrees: Float,
                       localBounds: CGRect) -> CGVector
    {
        guard !localBounds.isNull, !localBounds.isInfinite,
              localBounds.width > 0, localBounds.height > 0
        else {
            return .zero
        }
        let delta = CGPoint(x: localBounds.minX - anchor.dx,
                            y: localBounds.minY - anchor.dy)
        let scaled = CGPoint(x: delta.x * scale.dx, y: delta.y * scale.dy)
        let radians = CGFloat(rotationDegrees) * .pi / 180
        let cosine = cos(radians)
        let sine = sin(radians)
        return CGVector(dx: scaled.x * cosine - scaled.y * sine,
                        dy: scaled.x * sine + scaled.y * cosine)
    }

    static func toLayout(stored: CGVector, offset: CGVector) -> CGVector {
        CGVector(dx: stored.dx - offset.dx, dy: stored.dy - offset.dy)
    }

    static func toStored(layout: CGVector, offset: CGVector) -> CGVector {
        CGVector(dx: layout.dx + offset.dx, dy: layout.dy + offset.dy)
    }
}
```

- [ ] **Step 4: Run tests to verify they pass**

同 Step 2 命令。Expected: PASS。

- [ ] **Step 5: Commit**

```bash
git add apps/MotionStudioApp/MotionStudioApp/Inspector/LayoutPosition.swift \
  apps/MotionStudioApp/MotionStudioAppTests/LayoutPositionTests.swift
git commit -m "Add LayoutPosition helpers for top-left UI coordinates."
```

---

### Task 2: `MotionDocumentCore` layout 门面

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift`（在 `layerLocalBounds` 附近或 Transform 读写区新增）

**Interfaces:**
- Consumes: `LayoutPosition.offset/toLayout/toStored`；`layerLocalBounds`；`evaluateVec2` / `evaluateFloat`；`setStaticVec2` / `addKeyframeVec2`；`keyframes`
- Produces:
  - `func layoutPositionOffset(compositionID: UInt64, layerID: UInt64, frame: Int64) -> CGVector`
  - `func evaluateLayoutPosition(compositionID: UInt64, layerID: UInt64, frame: Int64) -> CGVector`
  - `func writeLayoutPosition(compositionID: UInt64, layerID: UInt64, frame: Int64, value: CGVector)`
  - `func keyframeLayoutPosition(compositionID: UInt64, layerID: UInt64, index: Int) -> CGVector`

- [ ] **Step 1: Add facade methods**

在 `MotionDocumentCore` 中 `layerLocalBounds` 之后插入：

```swift
/// Parent-space offset from stored position to local AABB top-left.
func layoutPositionOffset(compositionID: UInt64, layerID: UInt64, frame: Int64) -> CGVector {
    let anchor = evaluateVec2(entityID: layerID,
                              path: TransformProperty.anchorPoint.path,
                              frame: frame)
    let scale = evaluateVec2(entityID: layerID,
                             path: TransformProperty.scale.path,
                             frame: frame)
    let rotation = evaluateFloat(entityID: layerID,
                                 path: TransformProperty.rotation.path,
                                 frame: frame)
    guard let bounds = layerLocalBounds(compositionID: compositionID,
                                        layerID: layerID,
                                        frameTime: Double(frame))
    else {
        return .zero
    }
    return LayoutPosition.offset(anchor: anchor,
                                 scale: scale,
                                 rotationDegrees: rotation,
                                 localBounds: bounds)
}

func evaluateLayoutPosition(compositionID: UInt64, layerID: UInt64, frame: Int64) -> CGVector {
    let stored = evaluateVec2(entityID: layerID,
                              path: TransformProperty.position.path,
                              frame: frame)
    let offset = layoutPositionOffset(compositionID: compositionID,
                                      layerID: layerID,
                                      frame: frame)
    return LayoutPosition.toLayout(stored: stored, offset: offset)
}

/// Writes layout (top-left) position; converts to stored AE position.
/// Upserts keyframe when playhead already has one otherwise setStatic.
func writeLayoutPosition(compositionID: UInt64, layerID: UInt64, frame: Int64,
                         value: CGVector)
{
    let offset = layoutPositionOffset(compositionID: compositionID,
                                      layerID: layerID,
                                      frame: frame)
    let stored = LayoutPosition.toStored(layout: value, offset: offset)
    let path = TransformProperty.position.path
    if keyframes(entityID: layerID, path: path).contains(where: { $0.frame == frame }) {
        addKeyframeVec2(entityID: layerID, path: path, frame: frame, value: stored)
    } else {
        setStaticVec2(entityID: layerID, path: path, value: stored)
    }
}

func keyframeLayoutPosition(compositionID: UInt64, layerID: UInt64, index: Int) -> CGVector {
    let path = TransformProperty.position.path
    let frames = keyframeFrames(entityID: layerID, path: path)
    guard index >= 0, index < frames.count else { return .zero }
    let stored = keyframeVec2(entityID: layerID, path: path, index: index)
    let offset = layoutPositionOffset(compositionID: compositionID,
                                      layerID: layerID,
                                      frame: frames[index])
    return LayoutPosition.toLayout(stored: stored, offset: offset)
}
```

确保 `TransformProperty` 在该文件已可见（已有其它用法）。

- [ ] **Step 2: Build to verify compile**

优先 Xcode MCP `BuildProject`；不可用则：

```bash
xcodebuild -workspace MotionStudio.xcworkspace -scheme MotionStudioApp \
  -configuration Debug \
  -destination 'generic/platform=macOS,variant=Mac Catalyst,name=Any Mac' \
  ARCHS=arm64 build
```

Expected: BUILD SUCCEEDED。

- [ ] **Step 3: Commit**

```bash
git add apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift
git commit -m "Expose layout position read and write on MotionDocumentCore."
```

---

### Task 3: `TransformInspector` 接入 layout position

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/TransformInspector.swift`

**Interfaces:**
- Consumes: `evaluateLayoutPosition`、`writeLayoutPosition`
- Produces: Position X/Y 行显示/编辑 layout 坐标；锚点预设与其它属性仍写存储坐标

- [ ] **Step 1: Switch position rows to layout facade**

将 body 内：

```swift
let position = core.evaluateVec2(entityID: layerID,
                                 path: TransformProperty.position.path,
                                 frame: playheadFrame)
```

改为：

```swift
let position = core.evaluateLayoutPosition(compositionID: compositionID,
                                           layerID: layerID,
                                           frame: playheadFrame)
```

将 Position X/Y 的 `setVec2Property(.position, ...)` 改为专用写入（保留 keyframe 切换逻辑）：

```swift
NumberPropertyRow(label: TransformField.positionX.label,
                  value: Float(position.dx),
                  hasKeyframeAtPlayhead: hasKeyframe(.position),
                  isEditable: positionEditable)
{ newValue in
    setLayoutPosition(CGVector(dx: CGFloat(newValue), dy: position.dy))
} onToggleKeyframe: { _ in
    toggleVec2Keyframe(.position)
}

NumberPropertyRow(label: TransformField.positionY.label,
                  value: Float(position.dy),
                  hasKeyframeAtPlayhead: hasKeyframe(.position),
                  isEditable: positionEditable)
{ newValue in
    setLayoutPosition(CGVector(dx: position.dx, dy: CGFloat(newValue)))
} onToggleKeyframe: { _ in
    toggleVec2Keyframe(.position)
}
```

新增：

```swift
private func setLayoutPosition(_ value: CGVector) {
    performSet(.position) {
        core.writeLayoutPosition(compositionID: compositionID,
                                 layerID: layerID,
                                 frame: playheadFrame,
                                 value: value)
    }
}
```

**保持不变：**

- `applyAnchorPreset` 仍 `writeVec2(.position, value: newPosition)`（存储坐标补偿）
- `toggleVec2Keyframe(.position)` 仍对**存储**求值打点：

```swift
let value = core.evaluateVec2(entityID: layerID, path: property.path, frame: playheadFrame)
```

- [ ] **Step 2: Build**

同 Task 2 Step 2。Expected: BUILD SUCCEEDED。

- [ ] **Step 3: Commit**

```bash
git add apps/MotionStudioApp/MotionStudioApp/Inspector/TransformInspector.swift
git commit -m "Show and edit Transform position as layout top-left."
```

---

### Task 4: `MotionPathInspector` 关键帧点按 layout 显示

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/MotionPathInspector.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/InspectorView.swift`

**Interfaces:**
- Consumes: `keyframeLayoutPosition(compositionID:layerID:index:)`
- Produces: CubicBezierPad 的 `p0/p3`（及由其推出的 `c1/c2`）在 layout 空间；切线相对位移不变（段内 offset 恒定时精确）

- [ ] **Step 1: Pass `compositionID` into `MotionPathInspector`**

`MotionPathInspector` 增加：

```swift
let compositionID: UInt64
```

`InspectorView` 调用改为：

```swift
MotionPathInspector(core: core,
                    compositionID: core.firstCompositionID,
                    layerID: layerID,
                    selectedKeyframeIndex: editorState.motionPathSelectedKeyframe,
                    isEditable: isEditable,
                    perform: perform,
                    onSelectKeyframe: { index in
                        editorState.motionPathLayerID = layerID
                        editorState.motionPathSelectedKeyframe = index
                    })
```

- [ ] **Step 2: Convert keyframe endpoints to layout**

将：

```swift
let p0 = core.keyframeVec2(entityID: layerID, path: path, index: startIndex)
let p3 = core.keyframeVec2(entityID: layerID, path: path, index: endIndex)
```

改为：

```swift
let p0 = core.keyframeLayoutPosition(compositionID: compositionID,
                                     layerID: layerID,
                                     index: startIndex)
let p3 = core.keyframeLayoutPosition(compositionID: compositionID,
                                     layerID: layerID,
                                     index: endIndex)
```

`writeSegment` 仍用传入的 `p0/p3` 与 `c1/c2` 算相对切线并 `setSpatialTangents`——**不要**把切线再加 offset（相对量）。段内 offset 变化时的近似限制见 spec，此处不额外处理。

- [ ] **Step 3: Build**

同 Task 2 Step 2。Expected: BUILD SUCCEEDED。

- [ ] **Step 4: Commit**

```bash
git add apps/MotionStudioApp/MotionStudioApp/Inspector/MotionPathInspector.swift \
  apps/MotionStudioApp/MotionStudioApp/Inspector/InspectorView.swift
git commit -m "Display motion path keyframe points in layout coordinates."
```

---

### Task 5: Spec 状态 + 验收核对

**Files:**
- Modify: `docs/superpowers/specs/2026-08-03-layout-position-ui-design.md`

**Interfaces:**
- Consumes: Tasks 1–4 已完成行为
- Produces: spec 状态更新

- [ ] **Step 1: Update spec status**

将文首状态改为：

```markdown
状态：已实现（待人机验收）
```

- [ ] **Step 2: Manual acceptance checklist**

在 App 中核对（不写自动化）：

1. 新建 rect / image，Inspector Position 设为 `(0, 0)` → 视觉左上角贴合成 `(0, 0)`
2. 改锚点九点预设 → 外形不动，Position 数字变化
3. 画布拖拽图层 → Inspector Position 与视觉左上角一致
4. （可选）带 position 关键帧时打开 Motion Path pad，端点与 Inspector 数字一致

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/specs/2026-08-03-layout-position-ui-design.md
git commit -m "Mark layout position UI spec implemented pending acceptance."
```

---

## Spec Coverage Self-Review

| Spec 要求 | Task |
|---|---|
| UI-only，存储 AE 语义不变 | 全局约束 + Task 2/3 只换 UI 路径 |
| 全数字入口一致 | Task 3 Inspector + Task 4 Motion Path |
| AABB min + 实际 anchor/scale/rotation | Task 1 offset 公式 |
| bounds 缺失退化 | Task 1 empty bounds + Task 2 guard |
| 锚点预设仍写存储 | Task 3 明确保持 |
| FreeTransform 不改 | 无对应改动任务 |
| 添加关键帧打存储值 | Task 3 toggle 保持 evaluateVec2 |
| 纯函数测试用例 1–5 | Task 1 |
| 手动验收 | Task 5 |

无 TBD / 占位步骤。类型名在任务间一致：`LayoutPosition`、`evaluateLayoutPosition`、`writeLayoutPosition`、`keyframeLayoutPosition`、`layoutPositionOffset`。
