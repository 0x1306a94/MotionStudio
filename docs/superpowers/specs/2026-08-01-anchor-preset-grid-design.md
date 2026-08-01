# Transform 属性面板 — 九点锚点快捷设置

日期：2026-08-01  
状态：已确认；实现计划见 `docs/superpowers/plans/2026-08-01-anchor-preset-grid.md`

## 目标

在单选图层的 Transform Inspector 中，提供 **示意线框 + 可点击九点** 的锚点快捷设置：

1. 点击九点之一，将 `transform.anchorPoint` 设为图层局部内容框对应角/边中/中心
2. **补偿 `transform.position`**，使图层画面位置不变（与画布拖锚点一致）
3. 当前锚点落在某预设（容差内）时高亮该点；自定义位置则无一高亮
4. 局部内容框经 **bridge API** 获取，与 selection handles 的 `localMin`/`localMax` 同源（点文本为字形测量尺寸）

## 非目标

- 多选图层的九点控件
- 替换或移除 Anchor X / Anchor Y 数值行
- 改变画布上锚点拖拽交互
- 为「设锚点预设」单独新增 Core command 类型（复用现有 setStatic / addKeyframe）

---

## §1 UI 与交互

位置：`TransformInspector` 内，**Anchor X / Anchor Y 上方**。

控件 `AnchorPresetFrame`：

- **示意线框**：固定尺寸的矩形描边（非按图层真实宽高比拉伸）
- **9 个可点击点**：四角、四边中点、中心；命中区域略大于圆点视觉尺寸
- **高亮**：匹配预设时实心 / accent；未匹配时为空心点
- **点击已高亮点**：no-op
- **`isEditable == false`**（隐藏或锁定）：控件 disabled
- **无法取得 local bounds**：隐藏九点（数值行仍显示）

Inspector 现有逻辑已是单选（`selectedLayerID`）；本功能仅在该路径下出现。

---

## §2 Bridge：局部 bounds API

新增薄查询 API（不写文档、不做 mutation）：

```c
// 图层局部坐标 AABB，与 BuildSelectionHandles 单选时的 localMin/localMax 同源。
// 实现：EvaluatePreview → ResolvePointTextContainerSizes → BoundsOfLayerLocal。
// 找不到图层或无 bounds 时返回 false。
bool ms_layer_local_bounds(MSDocument *document,
                           uint64_t compositionId,
                           uint64_t layerId,
                           double frameTime,
                           float *minX, float *minY,
                           float *maxX, float *maxY);
```

Swift 封装：

```swift
func layerLocalBounds(compositionID: UInt64,
                      layerID: UInt64,
                      frameTime: Double) -> CGRect?
```

| 图层类型 | local bounds 语义（由 Core 保证） |
| --- | --- |
| Image / 框文本 | 内容 `size` 矩形 |
| 点文本 | 字形测量尺寸（经 `ResolvePointTextContainerSizes`） |
| Shape | 路径 / 几何局部 AABB |
| 其它无 bounds | API 返回 false → UI 隐藏九点 |

不复用 `ms_composition_layer_bounds`（那是**合成空间** AABB）。不复用完整 `selection_handles`（Inspector 不需要 chrome）。

---

## §3 Swift 逻辑：`AnchorPreset`

纯计算，无 bridge 副作用：

```swift
enum AnchorPresetCorner: CaseIterable {
    case topLeft, topCenter, topRight
    case middleLeft, center, middleRight
    case bottomLeft, bottomCenter, bottomRight
}

enum AnchorPreset {
    static func point(corner: AnchorPresetCorner, in rect: CGRect) -> CGVector
    static func matchingCorner(anchor: CGVector,
                               rect: CGRect,
                               tolerance: CGFloat = 0.5) -> AnchorPresetCorner?
    /// Δlocal = new − old；Δscene = rotate(scale(Δlocal))；return oldPosition + Δscene
    static func compensatedPosition(oldAnchor: CGVector,
                                    newAnchor: CGVector,
                                    position: CGVector,
                                    scale: CGVector,
                                    rotationDegrees: Float) -> CGVector
}
```

补偿公式与 `FreeTransformDrag.applyAnchor` / `compensatedPosition` 一致，保证画面不跳。

### 写入规则

一次 undo 单元（如 `"Set Anchor"`）：

1. 读 playhead 上的 `anchor` / `position` / `scale` / `rotation`
2. `newAnchor = point(corner:in: localBounds)`
3. `newPosition = compensatedPosition(...)`
4. 对 `anchorPoint`、`position` 分别：若该属性在 playhead 已有关键帧则 `addKeyframeVec2`，否则 `setStaticVec2`

---

## §4 文件与接线

| 区域 | 变更 |
| --- | --- |
| Bridge header + 实现 | `ms_layer_local_bounds` |
| `bridge_test` | Image / 点文本 / 框文本 / Shape 的 local bounds |
| `MotionDocumentCore` | `layerLocalBounds` |
| 新建 `AnchorPreset.swift` | corner / 匹配 / 补偿 |
| 新建 `AnchorPresetFrame.swift` | 线框九点 UI |
| `TransformInspector` | 接收 `compositionID`；挂控件；点击写入 |
| `InspectorView` | 传入 `compositionID`（与 FollowPath 一致，可用当前 composition） |

---

## §5 测试

- **bridge_test**：`ms_layer_local_bounds` 对各图层类型返回与 `BoundsOfLayerLocal` 一致的矩形；点文本非占位 400×120
- **Swift 纯函数**（若工程已有 Swift 单测入口则加；否则实现阶段以手动验证补偿 + 匹配为主）：补偿后等效于只改锚点且画面不动；容差匹配；无 bounds 隐藏

---

## 决策摘要

| 项 | 选择 |
| --- | --- |
| 视觉位置 | 补偿 position（不跳） |
| 高亮 | 容差匹配当前预设 |
| 作用范围 | 单选；Image / 文本 / Shape（有 local bounds 时） |
| 点文本尺寸 | Core 测量（经新 API） |
| UI 位置 | Anchor X/Y 上方 |
| UI 形态 | 示意线框 + 可点击点 |
| Bounds 来源 | 新 API `ms_layer_local_bounds`，非 Swift 拼装 |
| 架构 | Swift 共享 `AnchorPreset` + 线框控件；Core 只提供 bounds 查询 |
