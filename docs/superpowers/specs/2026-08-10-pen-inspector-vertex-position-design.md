# Pen Inspector Vertex Position — 设计说明

日期：2026-08-10  
状态：已实现  
范围：钢笔模式下选中路径点时，Inspector Position X/Y 显示/编辑该点场景坐标；ESC 先取消选点再退出钢笔

相关：`PathEditTarget` / `TransformInspector` / `networkEditMoveVertex` / `exitPenTool`

## 目标

1. 钢笔模式选中顶点时，Inspector **Position X/Y** 显示该点的**场景坐标**，调整时移动该点（不是图层包围盒）
2. 写入走场景坐标 → 局部坐标转换（与画布拖点一致）
3. ESC：有选中点时先取消选中；再按一次才退出钢笔模式

## 非目标

- 改 Anchor / Scale / Rotation / Opacity 行为
- 为顶点新增独立 Inspector 区块或改标签文案
- 多选顶点
- 箭头键 nudge 改绑顶点（本次仅 Position 字段）
- 切换 Select 工具时的两步确认（仍直接退出钢笔）

## 已锁定决策

| 项 | 选择 |
|---|---|
| 坐标系 | 场景坐标显示/编辑；写入时转局部（方案 B） |
| 无选中点 | Position 仍绑包围盒左上角（现有 layout position） |
| 关键帧按钮 | 选中点时隐藏 |
| 实现路径 | TransformInspector 条件切换（方案 1） |
| 写 API | 复用 `networkEditMoveVertex(scenePoint:)` |
| ESC | 仅 `exitPenTool` 两步；`finishPenTool` / 切工具仍一步退出 |
| 读失败 | Position 回退包围盒 |

---

## §1 行为

| 状态 | Position X/Y | 关键帧按钮 |
|---|---|---|
| 非钢笔，或钢笔但无选中点（`activeVertexId == 0`） | 图层包围盒左上角（`evaluateLayoutPosition` / `writeLayoutPosition`） | 绑 `transform.position` |
| 钢笔 + 有选中点 | 该点场景坐标 | 隐藏（`showsKeyframeButton = false`） |

Mask / Shape 路径同一套逻辑。

### ESC

仅 Escape → `exitPenTool`：

1. `tool == .pen` 且 `pathEditTarget.activeVertexId != 0` → `clearVertexSelection()`（含 `drawing = false`），留在钢笔，不 recenter
2. 否则 → 现有 `finishPenTool()`（shape recenter + `clearPathEdit` → select）

`activateSelectTool` / `togglePenTool` 退出仍直接 `finishPenTool()`。

---

## §2 架构与数据流

```
InspectorView
  └─ TransformInspector(pathEditTarget?)
        │
        ├─ 有选中点?
        │     读: pathEditVertexScenePosition
        │         evaluateVectorNetwork → local point
        │         × layer worldTransform → scene
        │     写: networkEditMoveVertex(scenePoint:)
        │         bridge ScenePointToLocal → MoveVertex
        │
        └─ 否则: evaluateLayoutPosition / writeLayoutPosition
```

| 组件 | 职责 |
|---|---|
| `TransformInspector` | Position 行条件切换读/写与关键帧按钮可见性 |
| `InspectorView` | 传入当前 `pathEditTarget`（钢笔且层匹配时） |
| `MotionDocumentCore.pathEditVertexScenePosition` | 局部点 → 场景坐标 |
| Bridge `ms_layer_transform_local_point`（若尚无等价 API） | 与 `ScenePointToLocal` 对称的 local→scene |
| `exitPenTool` | ESC 两步取消 |

---

## §3 接口

### Core（Swift）

```swift
/// 选中顶点的场景坐标；顶点/图层缺失时返回 nil
func pathEditVertexScenePosition(
    layerID: UInt64, path: String, frame: Int64, vertexId: UInt32
) -> CGPoint?
```

伪代码：

```
network = evaluateVectorNetwork(entityID: layerID, path: path, frame: frame)
local = network.vertex(id: vertexId)?.point
world = layerWorldTransform(layerID, frame)  // SceneEvaluator 路径，与拖点一致
return world.transformPoint(local)
```

### Bridge（按需）

若 Swift 侧无法复用现有 worldTransform 暴露：

```c
bool ms_layer_transform_local_point(
    MSDocument *document, uint64_t layerId, int64_t frame,
    float localX, float localY,
    float *sceneX, float *sceneY);
```

实现复用 path edit bridge 内已有的 `LayerWorldTransform` / `SceneEvaluator` 路径，保证与 `ScenePointToLocal` 互逆。

### 写（不新增）

```swift
core.networkEditMoveVertex(
    layerID: target.layerID,
    kind: target.kind,
    maskIndex: target.maskIndex,
    frame: playheadFrame,
    vertexId: target.activeVertexId,
    scenePoint: CGPoint(x: newX, y: newY))
```

### TransformInspector 接线

```swift
// InspectorView → TransformInspector 传入 optional PathEditTarget?
let editingVertex = tool == .pen
    && target?.layerID == layerID
    && target?.activeVertexId != 0

if editingVertex, let scene = core.pathEditVertexScenePosition(...) {
    // Position X/Y = scene; showsKeyframeButton = false
    // onCommit → networkEditMoveVertex
} else {
    // 现有 layout position + keyframe
}
```

读返回 `nil` 时回退包围盒分支。

### ESC

```swift
@objc func exitPenTool() {
    guard editorState.tool == .pen else { return }
    if var target = editorState.pathEditTarget, target.activeVertexId != 0 {
        target.clearVertexSelection()
        editorState.pathEditTarget = target
        return
    }
    finishPenTool()
}
```

---

## §4 边界与测试

- 删除顶点后已 `clearVertexSelection` → ESC 直接退出钢笔
- `drawing == true` 且 `activeVertexId != 0`：算有选中；ESC 一并清 drawing
- ESC 清选中不进 undo；改点走现有 path-edit 命令
- 画布 chrome 随 `selectedVertex = -1` 刷新（现有 `pathEditTarget` 观察路径）

**验证：**

1. 选点后 Position 数值与画布上该点场景位置一致；改 X/Y 点移动、包围盒不整体平移（除非点移动导致）
2. 画布拖点后 Inspector 数值同步
3. 无选中点时 Position 仍为包围盒；关键帧按钮恢复
4. ESC：选中点 → 取消选中仍钢笔 → 再 ESC 退出钢笔
5. 切 Select 工具仍一步退出钢笔

---

## §5 实现备注

- 尽量不改 Core 数据模型；转换与写命令留在 bridge / App
- 标签仍为 Position X/Y，不改文案
- `VertexMirroringInspector` 保持现有显示条件不变
