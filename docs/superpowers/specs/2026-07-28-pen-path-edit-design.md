# 钢笔路径自由编辑 — 设计说明

日期：2026-07-28  
状态：实现中（App 待人机验证后提交）  
分支：`feature/0x1306a94_pen_path`

## 目标

为编辑器提供 **完整钢笔工具**：新建自由路径、在边上插点、删点、拖拽顶点与切线手柄。  
同一套编辑器支持 **ShapePath** 与 **Mask path**。

权威路径模型保持现有 `BezierPath`（顶点 + Lottie 切线约定）。**不**改为 SVG 指令序列（SVG 适合交换/导出，不适合钢笔编辑）。

## 已锁定决策

| 项 | 选择 |
|---|---|
| 能力 | 新建、加点、删点、拖顶点/切线手柄；Inspector Mirroring（None/Angle/AngleLength） |
| 目标 | ShapePath + Mask path |
| 动画写回 | playhead：静态 `SetStaticValue`；已动画则 upsert `AddKeyframe` |
| Rect / Ellipse | 进入钢笔时不可逆烘焙为 ShapePath |
| 进入方式 | 见下方「进入钢笔」；Mask **仅** Inspector |
| 架构 | Core `PathEditHandles`（仿 SelectionHandles）+ 复用 PathOverlay |
| 手柄联动 | 默认拖一侧镜像对侧；Alt 断开（零切线 = corner，不单独存标志） |
| 锚点 | 创建完成（闭合 / 退出钢笔）后 shape path bounds 中心对齐局部原点，position 补偿，世界轮廓不变 |

## 非目标

- 多子路径 / compound path
- 路径布尔运算、方向反转
- Rect/Ellipse 可逆还原
- 时间轴曲线编辑器面板
- 触摸专用手势细化
- 升 schema
- 画布双击进入 Mask 编辑（Mask 只走 Inspector）

## 架构

```
Swift EditorTool.pen + PathEditTarget
  → Bridge (BezierPath ABI / convert / canvas target)
  → Core PathGeometryEdit（纯几何）
  → Core PathEditHandles（hit + chrome DrawCommands）
  → Undo: SetStaticValue | AddKeyframe | ConvertGeometryToPath
  → Model: ShapePath.path | Mask.path
```

画布合成（扩展现有 path-overlay 管线）：

```
内容
  → Mask PathOverlay（黄）
  → PathEdit chrome（顶点 / 手柄）
  → Selection chrome（钢笔态跳过，避免抢 hit）
```

绘制中未提交的开放路径预览走 `ms_canvas_set_path_overlays`（path-overlay spec 已预留）。

## 核心接口

### PathGeometryEdit（local space，无 Document）

```cpp
BezierPath MoveVertex(BezierPath path, size_t index, Vec2 newPoint, bool linkedHandles);
BezierPath MoveInTangent(BezierPath path, size_t index, Vec2 newIn, bool mirrorOut);
BezierPath MoveOutTangent(BezierPath path, size_t index, Vec2 newOut, bool mirrorIn);
BezierPath InsertVertexOnSegment(BezierPath path, size_t segmentIndex, float t);
BezierPath RemoveVertex(BezierPath path, size_t index);
BezierPath ClosePath(BezierPath path);
BezierPath AppendVertex(BezierPath path, BezierPath::Vertex vertex);
BezierPath ToggleVertexSmooth(BezierPath path, size_t index);  // 角点↔平滑
BezierPath RecenterPath(BezierPath path, Vec2 &localCenterOut); // bounds 中心→原点
```

插点复用 `PathResample` 中的 de Casteljau 分割语义。

`ToggleVertexSmooth`：近零切线视为角点 → 按邻点方向自动生成 1/3 边长切线；否则清零为角点。

`RecenterPath`：顶点平移使 AABB 中心到原点；切线为相对偏移，不变。Bridge 在 Shape 闭合/退出时对 `transform.position` 加上 `worldTransform.transformVector(localCenter)`，世界外形不变。

### PathEditHandles（世界系 chrome）

```cpp
enum class PathEditKind { Shape, Mask };
struct PathEditTarget { PathEditKind kind; EntityId layerId; int maskIndex; };

enum class PathHandleKind {
    None, Vertex, InTangent, OutTangent, Segment, CloseRing
};

struct PathEditHandles { /* world caches + selectedVertex + localPath + worldTransform */ };

bool BuildPathEditHandles(...);
PathEditHit HitTestPathEdit(...);
DrawCommandList BuildPathEditCommands(...);
```

**Hit 优先级（已修订）：**

1. 选中顶点的 In/Out 切线手柄（非零切线）
2. **顶点独占区**（约 8pt 半径，世界系换算）：区内一律 Vertex / CloseRing，**不**让位给 Segment（避免点顶点变成插点）
3. Segment（约 6pt）：仅当落在所有顶点独占区外

开放路径且 ≥2 点时，index 0 的顶点 hit 为 `CloseRing`（单击闭合）；拖拽时 App 仍按 Vertex 0 移动。

**Chrome 颜色：** 路径/顶点描边黄色；切线杆与切线圆描边蓝色（与顶点框区分）；选中顶点填充黄色。

### 命令

- `ConvertGeometryToPathCommand`：playhead 烘焙 Rect/Ellipse → `ShapePath`；undo 还原旧 geometry
- 整路径写回：复用 `SetStaticValueCommand` / `AddKeyframeCommand`（`PropertyValue` 已含 `BezierPath`）
- 新建：`AddPathLayer`（空或单点 ShapePath + 默认 Fill）
- 闭合 Shape：`ClosePath` + `RecenterPath` + position 补偿（同一 merge group）
- 退出钢笔 Shape：`ms_command_path_edit_recenter_shape`（已居中则 no-op）

### Bridge（薄 ABI）

- `MSBezierPath` / `MSBezierVertex` + free
- `ms_property_static_bezier_path` / `evaluate` / `set_static` / `add_keyframe`
- `ms_command_convert_geometry_to_path` / `ms_command_add_path_layer`
- `ms_command_path_edit_*`（move / tangent / insert / remove / close / append / set_mirror_mode / recenter_shape）
- `ms_canvas_set_path_edit_target` / `ms_canvas_hit_path_edit` / `ms_canvas_set_path_overlays`

## 进入钢笔

| 入口 | 行为 |
|---|---|
| 工具栏钢笔 | `tool = .pen`；对当前选中层走 Shape path（Rect/Ellipse 先 convert）；**不**保留 Mask 目标 |
| Inspector Mask 行铅笔 | `tool = .pen` + `PathEditTarget.mask`（**唯一** Mask 入口） |
| 选择模式双击 | 见「画布双击」；只进 **Shape path**，可烘焙 Rect/Ellipse |

## UI / 手势

### creationToolbar

顺序（仅图标、无 title）：**选择 → 钢笔 → 矩形 → 椭圆 → 图片**。

- 选择 / 钢笔：互斥选中态（蓝底 + 蓝 tint）；圆角与工具栏同心（padding 4，按钮半径 = 工具栏半径 − padding，continuous）
- 钢笔模式下矩形 / 椭圆 / 图片：**置灰且不可点**
- Esc / 点选择：退出钢笔（Shape 时 recenter）

### 钢笔态交互

| 动作 | 行为 |
|---|---|
| 空白点击 | 无目标 → add path layer + 首点；有开放草稿 → AppendVertex |
| 点首点 CloseRing | ClosePath + Shape recenter 并提交 |
| 点边（顶点区外） | InsertVertexOnSegment |
| 按下顶点/手柄 | 立即选中；拖动写回（零延时 long-press，非 UIPan 阈值） |
| Inspector Mirroring | `SetVertexMirrorMode`（None / Angle / AngleLength）；见 vertex-mirroring spec |
| Delete | RemoveVertex（至少保留 2 点） |
| 钢笔态双击画布 | **不**触发合成居中 |

零切线角点选中后不显示切线手柄；切到 Angle / AngleLength 后才出现可拖手柄。Append 的点默认角点；边上插点因 de Casteljau 可带非零切线。

### 选择模式 · 画布双击

| 条件 | 行为 |
|---|---|
| 恰好选中 1 层，且双击命中该层 | 进钢笔，编辑其 **Shape path**（Rect/Ellipse 先 convert） |
| 未选中任何图层 | 合成 fit / 居中 |
| 有选中但未点中该层，或其它情况 | 无操作 |

Mask path **不**由画布双击进入。

## 工程流程

- 分支：`feature/0x1306a94_pen_path`
- C++ / Bridge / 单测：每任务测试通过后自动 commit
- App UI（Swift）：实现后不自动提交；人工跑 App 验证后再指示提交

## 测试

- `PathGeometryEdit*`：移点、镜像/Alt、插点、删点、闭合、ToggleSmooth、Recenter
- `PathEdit*`：hit 优先级（顶点区优先于边）、空 path、chrome 命令非空
- `ConvertGeometryToPath*`：Rect/Ellipse → Path；undo；已是 Path no-op
- Bridge：BezierPath round-trip；toggle + close recenter；动画 upsert
- 手动：Pen 建闭合路径；Inspector 切换 Mirroring；退出后锚点在形状中心；选择态双击 Rect 进钢笔；Mask 仅 Inspector；工具栏钢笔时创建按钮置灰

## 与既有 spec 关系

- 补齐 [path-overlay](2026-07-26-path-overlay-design.md) 与 [layer-masks](2026-07-26-layer-masks-design.md) 中「钢笔编辑后续」
- 复用 `PathOverlayItem` / `BuildPathOverlayCommands`；落地 `ms_canvas_set_path_overlays`
