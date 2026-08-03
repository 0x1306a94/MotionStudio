# 图层快捷对齐（Layer Align）— 设计说明

日期：2026-08-03  
状态：已实现（已验收）  

## 目标

在编辑器顶部 toolbar 提供类似 Figma 的 **6 向快捷对齐**（左 / 水平中 / 右 / 上 / 垂直中 / 下）：

| 选中 | 对齐基准 |
|---|---|
| 1 层 | 合成画幅 `(0,0)–(width,height)` |
| ≥2 层 | 选中层在**合成空间**的视觉 AABB **并集** |

无选中图层时整组控件**不展示**。对齐使用合成空间视觉包围盒（`ms_composition_layer_bounds`），写回存储 `transform.position`；**一次对齐 = 一个 undo merge 单元**。

## 已锁定决策

| 项 | 选择 |
|---|---|
| 入口 | 顶部 `topToolbar`，紧挨 **Inspector 开关左侧**（右侧区：`[spacer] [Align×6] [Inspector]`） |
| 无选中 | 隐藏对齐控件 |
| 基准 | 单选→合成；多选→选中层视觉 AABB 并集 |
| 包围盒 | 合成空间视觉 AABB（方案 A） |
| 能力 | 仅 6 向对齐；不做 Distribute |
| 锁定 / 隐藏 | 忽略锁定与隐藏：仍参与基准并移动（对齐功能不尊重 lock/visibility） |
| 实现 | App 纯逻辑 + 现有 bounds + 薄 Bridge 父空间换算（方案 1） |
| Undo | `beginMergeGroup` → 写各层 → `endMergeGroup`，再经 `perform("Align …")` 登记；多选一次 Undo 全部还原 |

## 非目标

- 均匀分布（Distribute spacing）
- Inspector 内对齐行、菜单项、键盘快捷键（可后续加）
- 改 Core `Transform` 语义、文件格式、导出
- 对齐时修改 scale / rotation / anchor
- `ShapeProperty.position`（形状内部偏移）

## 算法

### 边枚举

```swift
enum LayerAlignEdge {
    case left, horizontalCenter, right
    case top, verticalCenter, bottom
}
```

### 基准框 `target`

- 单选：`CGRect(x: 0, y: 0, width: compositionWidth, height: compositionHeight)`
- 多选：对各选中层取 `layerBounds(compositionID:layerID:frameTime:)`，求并集；某层无 bounds 则跳过；全部失败则 no-op

### 合成空间位移

层框 `b`：

```
left:              Δ = (target.minX - b.minX, 0)
horizontalCenter:  Δ = (target.midX - b.midX, 0)
right:             Δ = (target.maxX - b.maxX, 0)
top:               Δ = (0, target.minY - b.minY)
verticalCenter:    Δ = (0, target.midY - b.midY)
bottom:            Δ = (0, target.maxY - b.maxY)
```

已对齐时 Δ≈0，可跳过该层写入。

### 写回

```
Δparent = MapCompositionDeltaToParent(layer, Δ)
storedPosition' = storedPosition + Δparent
```

写入规则与 Transform 一致：playhead 已有 `transform.position` 关键帧则 upsert，否则 `setStatic`。

不改 layout-position UI 语义以外的存储约定：此处直接改**存储** AE `position`（加父空间位移），与 FreeTransform 一致。

## 架构

```
topToolbar Align buttons
  → EditorViewController.perform("Align …") {
        core.beginMergeGroup()
        core.alignLayers(compositionID, layerIDs, edge, frame)
        core.endMergeGroup()
     }
  → MotionDocumentCore.alignLayers
        → layerBounds / composition size
        → LayerAlign.unionBounds / compositionDelta
        → ms_layer_map_composition_delta
        → write transform.position (per layer)
```

## 核心接口

### Swift 纯函数

```swift
enum LayerAlign {
    static func unionBounds(_ rects: [CGRect]) -> CGRect?
    static func compositionDelta(edge: LayerAlignEdge,
                                 bounds: CGRect,
                                 target: CGRect) -> CGVector
}
```

### Bridge

```c
// Maps a composition-space translation into the layer's parent space
// (inverse of parent world linear transform). Identity when no parent.
// Returns false when layer/composition missing or parent transform not invertible.
bool ms_layer_map_composition_delta(MSDocument *document,
                                    uint64_t compositionId,
                                    uint64_t layerId,
                                    double frameTime,
                                    float dx, float dy,
                                    float *outParentDx, float *outParentDy);
```

### MotionDocumentCore

```swift
func alignLayers(compositionID: UInt64,
                 layerIDs: [UInt64],
                 edge: LayerAlignEdge,
                 frame: Int64)
```

内部：算 target → 每层 Δ → map → 写 position。调用方负责 merge group + `perform`。

## Toolbar 布局

现有右侧为 spacer + `inspectorToggleButton`。改为：

```
… | spacer | AlignLeft … AlignBottom | Inspector |
```

- 有选中：显示 Align 组  
- 无选中：`isHidden = true`（不占位或占位折叠，与 UIStackView 惯例一致：hidden arrangedSubview 不占空间）

图标用 SF Symbol（或资源图标），accessibilityLabel 分别为 Align Left / Horizontal Centers / Right / Top / Vertical Centers / Bottom。

## 测试

**纯函数**

1. `unionBounds` 两不相交矩形 → 正确并集  
2. 六向 `compositionDelta` 数值正确；已对齐 → `(0,0)`

**Bridge**

3. 无父级：`map_composition_delta(10,20)` → `(10,20)`  
4. 父级仅旋转 90°：合成 `(10,0)` → 父空间约 `(0,-10)`（按项目旋转方向约定断言）

**手动验收**

5. 单选矩形 → Align Left：视觉左边贴合成 x=0  
6. 两层 → Align Horizontal Centers：两层视觉中线与并集中线重合  
7. 锁定层被选中时仍被移动  
8. 多选对齐后 **一次 Undo** 全部回到原位  

## 实现顺序（概要）

1. `LayerAlign` + Swift 测试  
2. `ms_layer_map_composition_delta` + Bridge 测试  
3. `MotionDocumentCore.alignLayers`  
4. `topToolbar` 接线（Inspector 前）+ 选中显隐  
5. 手动验收 + 更新本 spec 状态  
