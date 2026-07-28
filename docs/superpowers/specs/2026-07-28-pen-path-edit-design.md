# 钢笔路径自由编辑 — 设计说明

日期：2026-07-28  
状态：已确认，可进入实现  
分支：`feature/0x1306a94_pen_path`

## 目标

为编辑器提供 **完整钢笔工具**：新建自由路径、在边上插点、删点、拖拽顶点与切线手柄。  
同一套编辑器支持 **ShapePath** 与 **Mask path**。

权威路径模型保持现有 `BezierPath`（顶点 + Lottie 切线约定）。**不**改为 SVG 指令序列（SVG 适合交换/导出，不适合钢笔编辑）。

## 已锁定决策

| 项 | 选择 |
|---|---|
| 能力 | 新建、加点、删点、拖顶点/切线手柄 |
| 目标 | ShapePath + Mask path |
| 动画写回 | playhead：静态 `SetStaticValue`；已动画则 upsert `AddKeyframe` |
| Rect / Ellipse | 进入钢笔时不可逆烘焙为 ShapePath |
| 进入方式 | Inspector 选 Mask **或** 画布点轮廓 / mask 描边 |
| 架构 | Core `PathEditHandles`（仿 SelectionHandles）+ 复用 PathOverlay |
| 手柄联动 | 默认拖一侧镜像对侧；Alt 断开（零切线 = corner，不单独存标志） |

## 非目标

- 多子路径 / compound path
- 路径布尔运算、方向反转
- Rect/Ellipse 可逆还原
- 时间轴曲线编辑器面板
- 触摸专用手势细化
- 升 schema

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
```

插点复用 `PathResample` 中的 de Casteljau 分割语义。

### PathEditHandles（世界系 chrome）

```cpp
enum class PathEditKind { Shape, Mask };
struct PathEditTarget { PathEditKind kind; EntityId layerId; int maskIndex; };

enum class PathHandleKind {
    None, Vertex, InTangent, OutTangent, Segment, CloseRing
};

struct PathEditHandles { /* world caches + selectedVertex + localPath + worldTransform */ };

bool BuildPathEditHandles(...);
PathHandleKind HitTestPathEdit(...);  // 优先级: 手柄 > 顶点 > CloseRing > Segment
DrawCommandList BuildPathEditCommands(...);
```

### 命令

- `ConvertGeometryToPathCommand`：playhead 烘焙 Rect/Ellipse → `ShapePath`；undo 还原旧 geometry
- 整路径写回：复用 `SetStaticValueCommand` / `AddKeyframeCommand`（`PropertyValue` 已含 `BezierPath`）
- 新建：`AddPathLayer`（空或单点 ShapePath + 默认 Fill）

### Bridge（薄 ABI）

- `MSBezierPath` / `MSBezierVertex` + free
- `ms_property_static_bezier_path` / `evaluate` / `set_static` / `add_keyframe`
- `ms_command_convert_geometry_to_path` / `ms_command_add_path_layer`
- `ms_canvas_set_path_edit_target` / `ms_canvas_hit_path_edit` / `ms_canvas_set_path_overlays`

## UI / 手势

| 动作 | 行为 |
|---|---|
| 切到 Pen + 选中 Rect/Ellipse | convert → Shape 目标 |
| Inspector 选 Mask 行 | Mask 目标 |
| 点 mask 描边 / 形状轮廓 | 切换目标 |
| 空白点击 | 无草稿 → add path layer + 首点；有开放草稿 → AppendVertex |
| 点首点 CloseRing | ClosePath 并提交 |
| 拖顶点 / 手柄 | 几何编辑 + 写回；`beginDrag`/`endDrag` 合并 |
| 点边 | InsertVertexOnSegment |
| Delete | RemoveVertex（至少保留 2 点） |
| Esc / Select | 清草稿与 path-edit target |

## 工程流程

- 分支：`feature/0x1306a94_pen_path`
- C++ / Bridge / 单测：每任务测试通过后自动 commit
- App UI（Swift）：实现后不自动提交；人工跑 App 验证后再指示提交

## 测试

- `PathGeometryEdit*`：移点、镜像/Alt、插点、删点、闭合
- `PathEdit*`：hit 优先级、空 path、chrome 命令非空
- `ConvertGeometryToPath*`：Rect/Ellipse → Path；undo；已是 Path no-op
- Bridge：BezierPath round-trip；动画 upsert
- 手动：Pen 建闭合路径；编辑 mask；烘焙矩形后拖顶点

## 与既有 spec 关系

- 补齐 [path-overlay](2026-07-26-path-overlay-design.md) 与 [layer-masks](2026-07-26-layer-masks-design.md) 中「钢笔编辑后续」
- 复用 `PathOverlayItem` / `BuildPathOverlayCommands`；落地 `ms_canvas_set_path_overlays`
