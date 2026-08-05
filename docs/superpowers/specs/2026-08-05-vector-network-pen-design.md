# Vector Network 钢笔续画 — 设计说明

日期：2026-08-05  
状态：Core/Bridge/导出已实现；App 钢笔会话待人机验证

关联：[钢笔路径自由编辑](./2026-07-28-pen-path-edit-design.md)

## 目标

将钢笔交互升级为 **Figma 式 Vector Network**：

1. 路径闭合后仍可续画：选中已有顶点 → 点空白或其它顶点 → 新增边
2. 顶点可被多条边共享；拖动共享点时所有关联边跟随
3. 共享点（度数 ≠ 2）禁用 mirroring
4. 权威数据从单环 `BezierPath` 替换为 `VectorNetwork`；渲染/导出前编译为多轮廓 Path

## 已锁定决策

| 项 | 选择 |
|---|---|
| 权威模型 | `VectorNetwork` 整体替换存盘/关键帧中的路径（方案 A） |
| Shape / Mask | 二者均改为 `Animatable<VectorNetwork>` |
| 切线存放 | 挂在边两端（`startTangent` / `endTangent`）；顶点只存 `point` |
| Mirroring | 仅顶点度数 == 2 时有效；度数 ≠ 2 设置无效 / UI 禁用 |
| Fill | 提取所有最小封闭面并全部填充 |
| Stroke | 描全部边（含内部边） |
| 关键帧 / morph | 对 `VectorNetwork` 插值（同拓扑）；**不对**编译后 Path 插值 |
| 异拓扑关键帧 | 不插值（hold） |
| 渲染结构 | Network（权威）+ 多轮廓 `BezierPath`（编译产物） |
| 导出 Lottie/PAG | 按当前帧编译拍平为 Path；丢失共享点语义 |
| Schema | **不升** `schemaVersion`；同版本双读静默转换旧 `BezierPath` |
| 提交 | 本文档与后续实现均按指示提交；**本阶段不自动 commit** |

## 非目标

- 区域级独立填色、布尔运算
- 导出格式保留 Vector Network
- 拓扑不一致关键帧的自动重拓扑 / 传播改拓扑到全部关键帧
- 触摸专用手势细化
- 升 `schemaVersion`
- 改变现有「退出钢笔时 Shape recenter」锚点约定（仍保留）

## 动机与现状问题

当前 Core `BezierPath` 为单环顶点序列 + `closed` 标志；`AppendVertex` 在 `closed == true` 时直接拒绝。钢笔闭合后无法从已有点继续连边，也无法表达「一点多边」的网络（你提供的 Figma SVG 即此类结构）。

## 架构

```
Swift pen session (activeVertexId, drawing)
  → Bridge (VectorNetwork ABI / path edit commands)
  → Core VectorNetworkEdit（拓扑编辑）
  → Core VectorNetworkCompile（fill faces + stroke edges → BezierPath）
  → Core PathEditHandles（hit + chrome，基于 Network）
  → Undo: SetStaticValue | AddKeyframe（PropertyValue 持 VectorNetwork）
  → Model: ShapePath.path | Mask.path = Animatable<VectorNetwork>

求值 / 渲染:
  evaluate VectorNetwork
    → CompileFillFaces → ShapeGeometry.path (multi-contour BezierPath)
    → Stroke 全部 Edge
    → BuildTgfxPath（multi moveTo/close）
```

## 数据模型

### VectorNetwork（权威）

```cpp
struct VectorNetwork {
    struct Vertex {
        uint32_t id = 0;
        Vec2 point;
    };
    struct Edge {
        uint32_t id = 0;
        uint32_t start = 0;  // vertex id
        uint32_t end = 0;    // vertex id
        // Lottie 风格：相对端点的 handle 偏移
        // 三次贝塞尔控制点 = start.point + startTangent, end.point + endTangent
        Vec2 startTangent;
        Vec2 endTangent;
    };

    std::vector<Vertex> vertices;
    std::vector<Edge> edges;
};
```

约定：

- `id` 在单个 Network 内唯一；关键帧间靠 **相同 id + 相同连接关系** 对齐 morph
- 不允许自环（`start == end`）；平行边（同起终点多条边）本阶段 **允许**（Figma 少见，实现不特殊禁止）
- 度数 = 关联边数量（起或终命中该顶点）
- 断开的多组件（多个连通片）合法：闭合后续画新组件，或非 drawing 时点空白开新 stroke

### BezierPath（编译产物，multi-contour）

```cpp
struct BezierPath {
    struct Vertex {
        Vec2 point;
        Vec2 inTangent;
        Vec2 outTangent;
    };
    struct Contour {
        std::vector<Vertex> vertices;
        bool closed = false;
    };
    std::vector<Contour> contours;
};
```

- 旧「单环 `vertices` + `closed`」仅作为迁移输入与测试夹具；运行时权威不再是它
- 提供辅助：`BezierPath FromSingleContour(...)`、`IsSingleContour()` 等，降低迁移期改动面
- `ShapeGeometry.path`、DrawCommand、tgfx builder 消费 multi-contour

### 模型字段

| 位置 | 类型 |
|---|---|
| `ShapePath::path` | `Animatable<VectorNetwork>` |
| `Mask::path` | `Animatable<VectorNetwork>` |
| `PropertyValue` 路径分支 | `VectorNetwork`（替换原 `BezierPath` 权威分支） |

## 编译：Network → Path

### Fill：`CompileFillFaces(network) → BezierPath`

对齐 Figma「视觉封闭即填充」（曲线交叉也算）：

1. 将每条三次边采样为折线；在折线段几何交点处切分，得到平面直线图（合并近邻采样点）
2. 在该平面图上建半边邻接（出边按**弦方向**极角排序），绕行提取 **bounded faces**
3. 每个有界面 → 一条 `closed` contour（折线顶点；fill 不保留原三次切线）
4. 开放链、桥边、各连通分量的无界外脸：**不**进入 fill contours
5. 退化（零面积、重复顶点）跳过或合并，保证适配器不崩溃

**Stroke 仍走原 Network 的三次边**（`CompileStrokeEdges`），不受平面化影响。

### Stroke

- 对 `edges` 中每一条边单独描边（或编译为 open contour 列表）
- 内部边与外轮廓边一并描出，对齐 Figma「面可填、边都可见」

### 求值管线

```
SceneEvaluator
  evaluate Animatable<VectorNetwork>
  fillGeometry  = MakePathGeometry(CompileFillFaces(network))
  strokeEdges   = network.edges（适配器或 Command 侧按边 stroke）
```

具体是「一条 DrawPath + 一条 StrokePath」还是「Fill 用 faces、Stroke 用边列表」在实现 plan 里定接口；语义固定为：**Fill=全部最小面，Stroke=全部边**。

## 关键帧与 morph

- 插值输入/输出均为 `VectorNetwork`
- **同拓扑**（vertex id 集合、edge id 集合、每条边的 `start`/`end` 一致）时：逐点插值 `point`，逐边插值两端切线
- **异拓扑**：不插值，**hold 左关键帧**（与「顶点数量不一致则无法 morph」的既有路径策略同向）
- 共享点只存在一份 `Vertex`，插值一次 → 不会在 morph 中被拆开
- **禁止**对 `CompileFillFaces` 结果做 path morph（会复制共享点）

编辑期：允许在已有关键帧时改拓扑（写出新关键帧）；仅相邻同拓扑关键帧之间可平滑 morph。

## 钢笔交互

### 会话状态（不入库）

| 字段 | 含义 |
|---|---|
| `activeVertexId` | 当前高亮续画起点 |
| `drawing` | 已选定起点、下一次点击将连边或切换 |

闭合环 **不**清除钢笔工具，也不锁定网络；仅结束「当前一笔」的 drawing 语义（可立刻再点顶点开新笔）。

### 命中优先级

1. 选中顶点相关边端切线手柄（该端可编辑时）
2. 顶点
3. 边（插点）
4. 空白

### 点击表

| 条件 | 行为 |
|---|---|
| 空网络 + 空白 | 新建 path 层（或空 Network）+ 首顶点；`active=该点`，`drawing=true` |
| `drawing` + 空白 | 新顶点 P，加边 `active→P`；`active=P` |
| `drawing` + 已有顶点 V（V≠active） | 加边 `active→V`（已存在则 no-op）；`active=V`；不新建点 |
| `drawing` + active 自身 | 结束当前笔：`drawing=false`，保持 `active` 高亮；不新建边 |
| 非 drawing + 已有顶点 V | 只切换高亮：`active=V`，`drawing=true`；不建边 |
| 非 drawing + 空白 | 新顶点作为新组件起点；`active=新点`，`drawing=true` |
| 点边（顶点区外） | 边上插点；选中新点 |

闭合：`drawing` 且点击能与 `active` 形成新环的已有点（含经典点回路径起点）时加边；之后仍可点任意顶点续画（需求 1 / 1.1 / 1.2 / 1.3）。

### 拖拽与 mirroring

- 拖顶点：更新该 `Vertex.point`；所有关联边跟随（需求 1.5）
- 拖切线：改对应 `Edge` 端 handle；仅度数 == 2 时默认 mirror 另一条边在该点的 handle；度数 ≠ 2 **禁用 mirroring**（需求 1.4）
- Alt：度数 == 2 时断开镜像

### 相对旧钢笔的行为变更

| 旧行为 | 新行为 |
|---|---|
| `closed` 后拒绝 Append | 闭合后可从任意顶点 `AddEdge` |
| 单环 CloseRing 仅首点 | 网络上任一点可作续画起点 |
| 顶点一对 in/out | 切线在边上；度数>2 无单一 mirror 对 |

`RecenterNetwork`：退出钢笔时对 **Network 全部顶点** 做 AABB 中心→原点平移 + `transform.position` 补偿；切线为相对偏移不变；世界外形不变（延续既有锚点约定）。

## 序列化（不升 schema）

`SchemaMigrator::currentVersion` 保持 **1**。

路径字段 JSON **双读**：

| 形态 | 识别 | 行为 |
|---|---|---|
| 旧 | 含 `vertices` + `closed`，无 `edges` | 静默转为 `VectorNetwork`（顺序顶点发新 id；相邻连边；`closed` 则首尾再连一边） |
| 新 | 含 `vertices` + `edges` | 直接反序列化为 Network |

保存一律写 Network 形。旧文件再存即变为 Network JSON；无需 migration step。

切线迁移：旧顶点 `outTangent` → 出边 `startTangent`；下一顶点 `inTangent` → 该边 `endTangent`。

## Bridge / ABI（方向）

- `MSVectorNetwork` / 顶点与边 POD + free
- 属性读写：`static` / `evaluate` / `set_static` / `add_keyframe` 走 Network
- Path edit 命令改为 Network 语义：`add_vertex`、`add_edge`、`move_vertex`、`move_edge_tangent`、`insert_on_edge`、`remove_vertex`、`remove_edge` 等
- Canvas hit / chrome 基于 Network；会话 `activeVertexId` 可由 App 持有或经 bridge 同步

细节表在 implementation plan 列出，与现有 `ms_command_path_edit_*` 一一对照迁移。

## 导出

Lottie / PAG 无 Vector Network：导出时对 **当前帧** `evaluate` → `CompileFillFaces` + stroke 边 → 既有 Path 导出路径。共享点在导出结果中拆成轮廓副本（可接受，已锁定）。

## 测试要点

- Network 编辑：闭合后续画、点已有点只切换高亮、空白新建点并连边
- 共享点拖动：所有关联边端点坐标一致
- 度数 > 2：mirroring no-op；度数 == 2：默认 mirror
- `CompileFillFaces`：你提供的「外轮廓 + 内部辐射边」类网络 → 多个 closed fill contour；开放链无 fill、有 stroke
- Morph：同拓扑共享点不裂；异拓扑 hold
- Serializer：旧 BezierPath JSON 静默加载为 Network，再保存为 Network；`schemaVersion` 仍为 1
- 回归：Rect/Ellipse 转 Path、Mask 钢笔、recenter、既有 path morph 单环用例经迁移后仍通过

## 与既有 spec 关系

- 扩展并部分修订 [pen-path-edit](./2026-07-28-pen-path-edit-design.md)：权威模型由单环 BezierPath 改为 VectorNetwork；闭合后可续画
- 修订 path morph：插值对象改为 Network；编译 Path 仅渲染
- path-overlay / PathEditHandles：chrome 与 hit 改为面向 Network 顶点与边

## 实现顺序（预告，正式步骤见 plan）

1. Core：`VectorNetwork` 类型 + 旧 BezierPath 转换 + 序列化双读
2. `CompileFillFaces` + multi-contour `BezierPath` + tgfx builder
3. `Animatable` / PropertyValue / ShapePath / Mask 切换 + morph
4. `VectorNetworkEdit` + PathEditHandles / Bridge
5. App 钢笔状态机（active / drawing）
6. 导出拍平适配
7. 测试与人机验证

---

Implementation plan：`docs/superpowers/plans/2026-08-05-vector-network-pen.md`（不自动 commit，除非另行指示）。

