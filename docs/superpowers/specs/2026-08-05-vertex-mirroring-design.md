# Vertex Mirroring（Inspector 三段式）— 设计说明

日期：2026-08-05  
状态：已实现（Core / Bridge / App）；人机验收 Mirroring 控件  
关联：[Vector Network 钢笔](./2026-08-05-vector-network-pen-design.md)、[钢笔路径自由编辑](./2026-07-28-pen-path-edit-design.md)

## 目标

将「双击顶点切换角点/平滑」改为 **Inspector 中 Figma 式三段 Mirroring 控件**，并把模式持久化到 `VectorNetwork.Vertex`。

三段语义（对齐 Figma）：

| 模式 | 含义 |
|---|---|
| `None` | 角点；两侧 handle 独立（切换时清零） |
| `Angle` | 共线（方向相反）；长度可不同 |
| `AngleLength` | 共线且等长（对侧 = −当前） |

## 已锁定决策

| 项 | 选择 |
|---|---|
| UI | Inspector 三段分段控件；去掉画布双击 toggle |
| 持久化 | `Vertex.mirrorMode` 入库 / 关键帧 |
| 点选效果 | 立刻改切线几何并写 mode |
| Alt 拖切线 | 仅本趟不镜像；不改 `mirrorMode` |
| 新建顶点默认 | `None` |
| 度数 ≠ 2 | 控件禁用；Core 几何 no-op（与 VN spec 一致） |
| Morph | `mirrorMode` hold 左关键帧（不插值枚举） |
| Schema | **不升** `schemaVersion`；缺字段 → `None` |
| 实现路径 | Core 持有 mode；`MoveEdgeTangent` 按 mode 约束 |

## 非目标

- 多选顶点批量设 mode
- 导出保留 `mirrorMode`（PAG/Lottie 仍拍平 Path）
- Corner radius 等其它 Figma Vector 字段
- 升 `schemaVersion`
- 强制 App UI 自动化单测（人机验收即可）

## 数据模型

```cpp
enum class VertexMirrorMode : uint8_t {
    None = 0,
    Angle = 1,
    AngleLength = 2,
};

struct VectorNetwork::Vertex {
    uint32_t id = 0;
    Vec2 point;
    VertexMirrorMode mirrorMode = VertexMirrorMode::None;
};
```

- JSON：`vertices[].mirrorMode` 为整数 `0|1|2`；缺省 / 非法 → `None`
- Bridge：`MSVectorNetworkVertex` 增加对应字段
- 插值：同拓扑逐点插 `point`、逐边插切线；`mirrorMode` 取左关键帧

## Core API

```cpp
// 始终写入 mirrorMode。度数 ≠ 2：两侧 handle 几何不变（UI 禁用）。
// 度数 == 2：
//   None         → 两侧 handle 清零
//   Angle        → 按邻点方向生成共线 handle（各侧长度 = 对应弦长/3）
//   AngleLength  → 共线且等长；公共长度 = (inChord/3 + outChord/3) / 2
VectorNetwork SetVertexMirrorMode(VectorNetwork network, uint32_t vertexId,
                                  VertexMirrorMode mode);

// mirror == false：Alt — 只改本侧 handle，不改 mirrorMode
// mirror == true：按该顶点 mirrorMode
//   None         → 只改本侧
//   Angle        → 对侧方向相反，保留对侧原长度（若对侧近零则用本侧长度）
//   AngleLength  → 对侧 = -tangent
VectorNetwork MoveEdgeTangent(VectorNetwork network, uint32_t edgeId, bool atStart,
                              Vec2 tangent, bool mirror);
```

替换关系：

- 画布双击 → 删除
- `ToggleVertexSmooth`：删除，或薄封装为 `SetVertexMirrorMode(None ↔ Angle)`；测试改走 `SetVertexMirrorMode`
- Bridge `ms_command_path_edit_toggle_smooth`：删除或改为调用 set；新增 `ms_command_path_edit_set_mirror_mode`

## Inspector / 交互

**显示条件**

- `tool == pen` 且 `pathEditTarget` 有选中顶点（`activeVertexId != 0`）
- Shape / Mask 共用
- 无选中顶点：整块 **隐藏**
- `VertexDegree != 2`：控件可见但 **disabled**，仍显示当前 mode

**布局**

- 放在 Path 关键帧行附近，标题 `Mirroring`，三段图标对齐 Figma（角点 / 非对称平滑 / 对称平滑）

**操作**

- 点选 → `perform("Set Vertex Mirroring")` → set mirror mode（立刻改几何）
- 拖切线：App 继续传 `mirror = !isAltPressed`；Core 在 `mirror==true` 时读 `mirrorMode`

## 序列化与兼容

- 写：始终写出 `mirrorMode`
- 读：缺字段 → `None`
- 不升 `schemaVersion`

## 测试与验收

**Core**

- `SetVertexMirrorMode`：None / Angle / AngleLength 几何；度数 ≠ 2 no-op
- `MoveEdgeTangent`：三种 mode + `mirror=false`
- JSON round-trip；缺字段默认 None

**Bridge**

- set / get mirrorMode
- 原 toggle_smooth 用例迁移或删除

**验收**

1. 度数=2 顶点可在 Inspector 切三段，几何立刻变
2. 拖切线遵守 mode；Alt 临时断开，mode 条不变
3. 度数≠2 控件禁用
4. 双击顶点不再切换平滑
5. 存盘重开 mode 仍在

## 实现范围（文件级预期）

| 层 | 触点 |
|---|---|
| Core | `VectorNetwork.h`、`VectorNetworkEdit`、序列化、插值（hold mode）、相关测试 |
| Bridge | ABI vertex 字段、set_mirror_mode、path_edit 拖切线链路、删/替 toggle_smooth |
| App | 去掉双击平滑；`InspectorView` + Mirroring 控件；读 mode / 写 set |
| Docs | 本 spec；实现后回写 VN / pen-path-edit 中「双击 toggle」表述 |
