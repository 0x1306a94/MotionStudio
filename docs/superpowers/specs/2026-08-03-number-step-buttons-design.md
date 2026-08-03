# 数字步进按钮 + 方向键微调 Position — 设计说明

日期：2026-08-03  
状态：已实现（待人机验收）  

## 目标

1. Inspector 数字行提供 **↑ / ↓** 微调（每次 ±1），默认关闭，仅对指定字段开启。  
2. 编辑器方向键对**选中层**做合成空间 Position 微移（±1），多选同位移且一次 Undo。

## 已锁定决策

### A. NumberPropertyRow 步进按钮

| 项 | 选择 |
|---|---|
| 布局 | `label \| TextField \| ↑↓ \| ◆(关键帧)` |
| 键盘 | **本组件不**拦截方向键 |
| 步进 | 固定 `step = 1` |
| 开关 | `showsStepButtons: Bool = false`；可选保留 `step: Float = 1` |
| 开启范围 | Transform：Anchor X/Y、Position X/Y；Shape Size：Width / Height |
| 关闭范围 | Scale、Rotation、Opacity、Shape Radius、其余调用方 |
| 不可编辑 | 步进按钮 `disabled` |

### B. 方向键微调 Position

| 项 | 选择 |
|---|---|
| 触发 | 有选中层；文本输入框聚焦时由系统消化方向键（不进 editor） |
| 映射 | ←：Δx = −1；→：Δx = +1；↑：Δy = −1；↓：Δy = +1（合成空间） |
| 多选 | 每层施加**相同**合成空间位移 |
| Undo | `beginMergeGroup` → 写各层 → `endMergeGroup`，再经 `perform("Nudge Position")`（或分向命名）登记；一次 Undo 全部还原 |
| 锁定 / 隐藏 | 忽略：仍参与并移动（同 Layer Align） |
| 父级 | `ms_layer_map_composition_delta` / `mapCompositionDelta` 后写存储 `transform.position` |
| 语义 | 只改 Position；不改 Anchor / Size / Scale 等 |

## 非目标

- NumberPropertyRow 内用方向键改当前字段  
- Shift 加速（×10）  
- 按属性配置不同 step（调用方本次不传自定义 step）  
- 在 `NumberPropertyRow` 内做 undo merge（按钮步进走各 Inspector 既有 `onCommit`）  
- Distribute / Align（已有独立功能）

## 核心接口

### NumberPropertyRow

```swift
struct NumberPropertyRow: View {
    // 既有参数…
    var showsStepButtons = false
    var step: Float = 1

    // nudge(+1/-1): base = parsedDraft() ?? value; onCommit(base ± step)
}
```

调用方：

| 文件 | 开启的行 |
|---|---|
| `TransformInspector.swift` | Anchor X/Y、Position X/Y |
| `ShapeSizeInspector.swift` | Width、Height |

### 方向键（Editor）

```swift
// EditorViewController.keyCommands += UIKeyCommand arrow inputs
// → nudgeSelection(dx:dy:)

// MotionDocumentCore（推荐集中逻辑，与 alignLayers 对称）:
func nudgeLayersPosition(compositionID: UInt64,
                         layerIDs: [UInt64],
                         delta: CGVector,   // composition space
                         frame: Int64)
// 每层: mapCompositionDelta → stored position += parentDelta
// 调用方负责 merge + perform
```

伪代码：

```swift
func nudgeSelection(dx: CGFloat, dy: CGFloat) {
    let layerIDs = editorState.selectedLayerIDs
    guard !layerIDs.isEmpty else { return }
    perform("Nudge Position") {
        document.core.beginMergeGroup()
        document.core.nudgeLayersPosition(compositionID: …,
                                          layerIDs: layerIDs,
                                          delta: CGVector(dx: dx, dy: dy),
                                          frame: playheadClock.frame)
        document.core.endMergeGroup()
    }
}
```

## 实现顺序（概要）

1. `NumberPropertyRow`：`showsStepButtons` / `step` + ↑↓ + `nudge`  
2. Transform / ShapeSize 打开开关  
3. `MotionDocumentCore.nudgeLayersPosition`  
4. `EditorViewController` 注册方向键 → merge nudge  
5. 编译 + 人机验收  

## 验收

**步进按钮**

1. Transform Position：点 ↑，对应轴 +1。  
2. Shape Width：点 ↓，宽度 −1。  
3. Rotation / Opacity / Radius：无步进按钮。  
4. 不可编辑时步进禁用。  

**方向键**

5. 单选：→ 视觉右移 1（合成空间）。  
6. 多选：一次 →，两层同移；一次 Undo 全部还原。  
7. 锁定层被选中时仍移动。  
8. Inspector 数字框聚焦打字时，方向键不挪图层（仍可在框内移动光标）。  
