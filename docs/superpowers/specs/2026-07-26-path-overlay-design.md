# 通用 Path Overlay（画布路径描边）— 设计说明

日期：2026-07-26  
状态：已确认，可进入实现

## 目标

为编辑器提供 **通用、预览专用** 的 path 描边能力：任意 `BezierPath` + 世界变换 → 画布叠加描边。  
首版消费者：选中层的 **path masks**（只显示、不可编辑）。后续钢笔自由编辑复用同一套 API。

## 非目标

- 顶点 / 手柄拖拽、钢笔工具交互
- Mask expansion / feather 可视化（描边使用求值后的 path 本身，与 AE 轮廓一致）
- Track Matte 轮廓（无 path）
- 升 schema

## 架构

与选中框同一层：**场景内容画完 → `restoreCompositionClip` → overlay / selection chrome**。  
不进入 `BeginLayer` / mask coverage，也不写入文档。

```
SceneState
  → BuildCommands (内容)
  → CollectMaskPathOverlays(selected) + canvas.extraPathOverlays
  → BuildPathOverlayCommands → StrokePath…
  → BuildSelectionOutlineCommands → 选中框
```

## 核心接口

```cpp
struct PathOverlayItem {
    Mat3 worldTransform;  // local path → world
    BezierPath path;      // local space
    Color color;
};

// Generic: items → flat StrokePath commands (no Save needed if ConcatTransform per item).
DrawCommandList BuildPathOverlayCommands(const std::vector<PathOverlayItem> &items,
                                         float strokeWidth);

// Mask consumer: selected layers' EvaluatedMask.path + layer.worldTransform.
std::vector<PathOverlayItem> CollectMaskPathOverlays(
    const SceneState &state,
    const std::vector<EntityId> &selectedLayerIds,
    Color color);
```

画布侧：

- 每帧：`CollectMaskPathOverlays(selected)` → `BuildPathOverlayCommands`，在选中框之前播放
- `ms_canvas_set_path_overlays`：**延后到钢笔工具**（BezierPath 过 C ABI 需单独设计）；扩展点是 Core 的 `PathOverlayItem` 列表合并，不是 mask 专用绘制

## 行为（首版）

| 条件 | 行为 |
|---|---|
| 选中层有 ≥1 path mask | 每条 mask 描一条轮廓 |
| 多选 | 各自 masks 都显示 |
| 无 mask / 未选中 | 不画 path overlay |
| 颜色 | 固定黄系（与蓝色选中框区分），约 `{1, 0.85, 0.2, 1}` |
| 线宽 | 与选中框同量级：`1.5 * sceneUnitsPerViewPoint` |

## 测试

- `BuildPathOverlayCommands`：空列表 / 单 path → `StrokePath` + `ConcatTransform`
- `CollectMaskPathOverlays`：未选中为空；选中有 mask 的层返回对应 path

## 与 layer-masks spec 关系

补齐 masks design §4「画布：选中层可显示 mask 路径」的 **显示** 半截；编辑仍属后续。
