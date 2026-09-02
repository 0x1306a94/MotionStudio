# ShapePath 缩放拖动：预览矩阵 — 设计说明

日期：2026-09-02  
状态：待确认  
分支：`feature/0x1306a94_path_resize`  
关联：[`2026-07-30-figma-style-resize-box-text-design.md`](./2026-07-30-figma-style-resize-box-text-design.md)

## 目标

ShapePath（含同层 mask 路径）在选中框角点/边中点拖动缩放时：

1. **拖动过程流畅**——不每帧重写 `VectorNetwork`、不每帧 `CompileVectorNetwork`
2. **松手后几何结果与现行为一致**——顶点/切线真变大，`transform.scale` 不变
3. **符合现有规范**——手柄最终仍禁止写 `transform.scale`（见关联 spec §1）

## 非目标

- 加速 `BuildCurvePlanarNetwork` / 交叉求交本身
- Rect / Ellipse / Image / Text / Group 的缩放通路
- 持久 Region、布尔、顶点级属性
- 拖动中 Inspector 数字实时同步（允许落后，松手后刷新）

## 动机与根因

现状（`FreeTransformDrag.applyShapeGeometryResize`）：每个 pointermove

1. 从拖动起点快照缩放全部顶点/切线
2. `writeVectorNetworkAtPlayhead` → Stamp 新 `GeometryRevision`
3. `contentRevision` 变 → PreviewSceneCache / FrameCommandCache 失效
4. `SceneEvaluator` → `CompileVectorNetwork` → `BuildCurvePlanarNetwork`（约 \(O(E^2\cdot S^2)\)，`S=16`）

文字转曲等稠密 path（约 200+ 边）在拖动中每帧千万级线段求交，必然掉帧。

Figma 类工具的做法：拖动中只叠 **preview matrix**，权威几何不动；松手再 bake 一次。

两份「慢 / 顺」SVG 拓扑边数实测几乎相同——体感差主要来自这条拖动热路径，而非导入焊接差异。

## 已锁定决策

| 项 | 选择 |
|---|---|
| 预览载体 | `MSCanvas` 会话状态：`layerId → Mat3`（场景空间），**不进 Document / undo / contentRevision** |
| 覆盖范围 | **仅** ShapePath 内容路径 + 同层 mask 路径的 `scaleCorner` / `scaleEdge` |
| 其它类型 | Rect/Ellipse/Image/Text/Group 保持现有每帧写 Document |
| 混选 | 选区中 ShapePath 层走预览；其它类型照旧写 Document |
| 矩阵语义 | **局部后乘**：`effectiveWorld = worldTransform * ScaleAboutLocalPivot(localPivot, sx, sy)`（与 bake 的局部仿射一致；旋转层不会被场景轴对齐 S 扭曲） |
| 选中框 | 显示用应用了 preview 的 handles；命中继续用 begin 时的 `startHandles` |
| Merge / undo | **`beginMergeGroup` 延后到松手 bake 前**；拖动中零 Document 命令 |
| 松手 | 可接受一次 Compile 短顿 |
| 关键帧 path | bake 仍走 `writeVectorNetworkAtPlayhead`（当前帧语义不变） |
| stroke 视觉 | 拖动中矩阵会放大线宽；松手后回到图层 stroke——接受与 Figma 几何缩放类似的差异 |

## 架构

```text
pointerdown (ShapePath 缩放手柄)
  capture LayerTransformStart（现有）
  previewActive = true
  // 不开 merge group

pointermove
  计算 scaleX/scaleY/pivotScene（现有）
  对每个 hasShapePath|mask 的层:
    localPivot = 与 applyShapeGeometryResize 相同的局部 pivot 换算
    L = T(localPivot) * S(scaleX, scaleY) * T(-localPivot)   // 局部仿射
    ms_canvas_set_preview_transform(canvas, layerId, L)         // 存局部后乘矩阵
  requestDraw()
    EnsurePreviewScene —— contentRevision 不变 → **几何求值缓存命中**（不重 Compile）
    若 previewTransforms 非空：
      浅拷贝 SceneState，对命中层 worldTransform = world * L
      **绕过 FrameCommandCache**，用拷贝现场 BuildCommands（命令里已烤死 ConcatTransform，不能复用旧命令）
      用同一拷贝 BuildSelectionOutlineCommands
    否则：走现有 EnsureSceneCommands 缓存

pointerup（有移动）
  beginMergeGroup()
  对每个相关层: bake 顶点/切线/mask + anchor/position 补偿（复用现数学）
  clear 该层 preview transform
  endMergeGroup(); registerEdit(...)

pointerup（无移动）/ cancel
  clear preview transforms；无 undo
```

## 接口

### Bridge C API

```c
// 局部后乘 Mat3，row-major 9 floats，布局与 motion::Mat3::values 一致。
// 语义：effectiveWorld = layer.worldTransform * M。
// 不修改 Document，不 bump contentRevision。
void ms_canvas_set_preview_transform(MSCanvas *canvas, uint64_t layer_id, const float m[9]);
void ms_canvas_clear_preview_transform(MSCanvas *canvas, uint64_t layer_id);
void ms_canvas_clear_all_preview_transforms(MSCanvas *canvas);
```

### Canvas 内部

```text
MSCanvas.previewTransforms: unordered_map<EntityId, Mat3>  // 局部后乘

EffectiveWorld(layer, canvas):
  W = layer.worldTransform
  if found = previewTransforms[layer.id]:
    return W * found
  return W
```

应用点（锁定）：

在 `ms_canvas_draw_frame_at_time_profiled`（及整数帧变体若共用）里，于 `EnsurePreviewScene` 成功后：

1. 若 `previewTransforms` 非空：复制 `state.layers`（或整份 `SceneState`），对命中层改写 `worldTransform = world * L`，再 `BuildCommands` + `BuildSelectionOutlineCommands`（**不读写** `FrameCommandCache`）；mask path overlay / path edit chrome 若读 `layer.worldTransform`，须用同一拷贝
2. 若为空：保持现有 `EnsureSceneCommands` 路径

不修改 Core `BuildCommands` 签名。几何仍来自求值缓存，避免 Compile。

## App 行为变更

### `FreeTransformDrag`

- `applyScale`：若 `start` 含 `shapePath` 或非空 `maskPaths`，**不**调用 `applyShapeGeometryResize`；改为返回/写入 preview 所需的 `(layerId, matrix)`（或由 VC 调 bridge）
- 新增 `commitShapeGeometryResize(...)`：把现 `applyShapeGeometryResize` 的 bake + anchor/position 逻辑挪到松手调用一次
- 非 Path 的 Shape（仅 `shapePosition/size`）与其它类型：保持现状

### `CanvasViewController`

- `beginHandleTransform`：ShapePath 缩放时**不要** `beginMergeGroup`
- `updateFreeTransform`：更新 preview matrix 后 `requestDraw`
- `endFreeTransform`：有移动则 `beginMergeGroup` → commit bake → clear preview → `endMergeGroup` → `registerEdit`；否则只 clear preview

## 正确性约束

1. bake 始终基于 **拖动起点快照** × 最终 scale（禁止累计已 preview 的中间态）
2. preview clear 必须在 bake 写回之后、下一次 draw 之前（或同帧先 bake 再 clear，避免闪回）
3. 空拖动（按下即抬起）不得产生 undo 项
4. Esc / 手势 cancel：丢弃 preview，Document 不变
5. 播放模式不启用该路径（现有 free-transform 本就不在播放中）

## 测试

| 层级 | 用例 |
|---|---|
| Bridge / render | set preview 后命令中层 transform 点被映射；clear 后恢复；`contentRevision` 不变 |
| Bake 数学 | 固定 pivot/scale 下顶点与切线与现 `CapturedVectorNetwork.scaled` 一致；anchor/position 补偿一致 |
| Undo | 一次缩放拖动 = 一个 undo 单元；undo 回到拖前几何与 transform |
| 人机 | 文字转曲 SVG（~200 边）拖角点流畅；松手可短顿；框与内容不脱节 |

## 风险与后续

| 风险 | 缓解 |
|---|---|
| 混选时选中框 union 与部分层 preview 不一致 | 第一期接受；若明显，可对 chrome 再乘选区级矩阵（不进 Document） |
| Bridge 浅拷贝 `SceneState` 成本 | 仅改 `worldTransform` 字段；层数通常很小 |
| 松手 Compile 仍顿 | 已接受；后续另开 Compile 优化（自适应采样等） |

后续可选项（不在本期）：非 Path 类型统一延迟 bake；Compile 求交加速；拖动中 stroke 宽度补偿。
