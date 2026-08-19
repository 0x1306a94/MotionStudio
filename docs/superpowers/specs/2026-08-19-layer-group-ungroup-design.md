# 图层编组 / 解组 + 时间轴父子缩进 — 设计说明

日期：2026-08-19  
状态：已确认，可进入实现计划  
分支：`feature/svg_importer`

## 目标

1. 把选中图层包进 `LayerType::Group`（编组），或拆掉选中的 Group（解组），画面在操作当下尽量不跳。
2. 一次操作一条 undo。
3. 时间轴 Layer 列按 `parentId` 缩进，父子关系可见。

对标 Figma Group / Ungroup，不是预合成。模型沿用已有扁平 `Composition.layers` + `parentId`。

## 已锁定决策

| 项 | 选择 |
|---|---|
| 编组资格 | ≥1 层；必须同一 composition、同一 `parentId`；去掉「已被其他选中层包含的子孙」后剩下的层拿去编组 |
| 嵌套 | 允许 Group 再套 Group |
| 解组对象 | 只解选中的 `LayerType::Group`；只选中子层不解 |
| 预合成 | 不编进、不解预合成；预合成层可以和其他层一起被包进 Group |
| Group transform | 新建为单位矩阵（position 0、anchor 0、scale 1、rotation 0、opacity 1） |
| 解组 bake | 当前帧；只改子层**无关键帧**的 position / rotation / scale；有关键帧的属性不改空间 |
| 默认名 | `"Group"`（重名允许） |
| 时间轴 | 按 `parentId` 深度缩进；**不做**折叠；拖拽/排列不改 `parentId` |
| 入口 | Arrange 菜单 + 时间轴右键；`⌘G` / `⇧⌘G` |

## 非目标

- 预合成的创建 / 解组 / Pre-compose
- 时间轴折叠 / 展开
- 拖到某层上改 parent（drop-to-parent）
- 单独的 Parent 拾取器（不新建 Group）
- 把子层关键帧乘上 Group 矩阵
- 带 skew 的完美矩阵分解
- 右侧轨道条做缩进（行对齐不变）
- 改文件 schema

---

## §1 编组

输入：当前合成 + 选中 layer id 列表 + 当前帧（仅用于读 timing，不写子层 transform）。

```
ids = unique(selection)
去掉 ids 里「parent 链上另有选中层」的子孙
若 ids 为空 → no-op
若不同 composition 或 parentId 不一致 → no-op

commonParent = 这些层的 parentId
topIndex = ids 在 layers[] 里的最大下标（最前）

新建 Group：
  name = "Group"
  parentId = commonParent
  transform = 单位
  inPoint = min(children.inPoint)
  outPoint = max(children.outPoint)

插入并重排，使模型序变成：
  [原先在 topIndex 之下且未选中的层]
  + ids 按原模型序（底→顶）
  + Group
  + [原先在 topIndex 之上且未选中的层]

对每个 id：setParent(Group)
选中仅 Group
undo 名 "Group"
```

Group 下标高于其子层 → 时间轴 `.reversed()` 后 Group 在子层之上，缩进后看起来像树。

`setParent` 失败（环、悬空）则整条命令不执行。

### 画面为何不跳

单位 Group 挂到原来的公共父级下：`world' = P * I * local = world`。

---

## §2 解组

只处理选中里 `type == Group` 的层（不含预合成）。多个 Group 一次解开。

对每个 Group G（从深层到浅层，避免先拆父再拆子乱序）：

```
P = G.parentId
children = parentId == G.id 的直接子层（保持模型序）
Glocal = G.localTransform(当前帧)

对每个 child：
  若 Glocal 不是单位：
    composed = Glocal * child.localTransform(当前帧)
    保留 child.anchor
    分解 composed → position / rotation / scale（见 §4）
    仅当对应 Animatable 无关键帧时写入静态值
  setParent(child, P)
RemoveLayer(G)

选中所有被放出的直接子层
undo 名 "Ungroup"
```

无选中 Group → no-op。

G 的 mask / track matte / 特效随层删除；子层 id 不变，指向子层的引用仍有效。指向 G 的 `trackMatteLayerId` 变成悬空，现有求值会忽略。

---

## §3 命令与 Bridge

新命令 `SetParentCommand(layerId, newParentId)`：execute 调 `Layer::setParent`，记下旧 parent；失败则该步 no-op。`CommandKind::SetParent`。

编组 / 解组是 `CompositeCommand`，由 Core 组装，不在 Swift 里拼：

```cpp
std::unique_ptr<Command> MakeGroupLayersCommand(
    const Document &document, EntityId compositionId,
    const std::vector<EntityId> &layerIds);

std::unique_ptr<Command> MakeUngroupLayersCommand(
    const Document &document, EntityId compositionId,
    const std::vector<EntityId> &layerIds, FrameTime time);
```

返回 `nullptr` 表示 no-op。子命令：`AddLayerCommand` / `MoveLayerCommand` / `SetParentCommand` / `SetStaticValueCommand` / `RemoveLayerCommand`。

Bridge（C 数组风格，对齐 `ms_canvas_set_selected_layers`）：

```c
// 成功：新 Group id。失败 / no-op：0，文档不变。
uint64_t ms_command_group_layers(MSDocument *document, uint64_t compositionId,
                                 const uint64_t *layerIds, size_t count);

// 成功：true。失败 / no-op：false，文档不变。
bool ms_command_ungroup_layers(MSDocument *document, uint64_t compositionId,
                               const uint64_t *layerIds, size_t count,
                               int64_t frame);
```

App 用现有 `layerParentID` / `layerType` 算菜单 enable，不必再加 can_* ABI。

Swift：`MotionDocumentCore.groupLayers` / `ungroupLayers`；成功后改选中。

---

## §4 解组矩阵分解

`local = T(position) * R(rotation) * S(scale) * T(-anchor)`。  
固定 `anchor`，从 `composed` 取：

- 线性 2×2 的列长 → `scale`
- `atan2` 第一列 → `rotation`（度）
- `position = composed.transformPoint(anchor)`

有 skew 时这是近似，v1 接受。负 scale 不特意还原成翻转，按列长取正 scale。

---

## §5 时间轴缩进

列表仍按图层数组倒序（顶 = 画面最前），**不**按树 DFS 重排。编组后的连续块（§1）让 Group 紧挨子层。

深度：沿 `parentId` 走到根，环则停。`depth=0` 为合成根。

| 行 | 左边距 |
|---|---|
| Layer 行 stack | `8 + depth * 12` |
| 属性 / 关键帧行 | `28 + depth * 12` |

眼睛 / 锁按钮不随缩进右移（仍贴行尾）。

SVG 导入的 Group 树同样缩进，无需另做。

不做 disclosure 三角、折叠、隐藏子层。

---

## §6 拖拽与 Arrange

`parentId` 不因拖拽或 Arrange 改变。

拖 Group 或 Arrange 选中 Group 时：moving 集合扩成 **该层 ∪ 全部子孙**，整块移动，避免把子树拆开。只拖某个子层时只动那一层（可以在列表里离开 Group 行，缩进仍表示它是子层）。

`TimelineReorder.movingIDsIncludingDescendants(order:parentOf:moving:)` 纯函数，单测锁行为。

---

## §7 UI 入口

| 位置 | Group | Ungroup |
|---|---|---|
| Arrange 菜单（`AppDelegate.buildMenu`） | Group `⌘G` | Ungroup `⇧⌘G` |
| 时间轴 Layer 行右键 | 同上 | 同上 |
| `canPerformAction` | 去子孙后 ≥1 且同父 | 选中含 Group |

导出进行中时与其它编辑命令一样禁用。

---

## §8 测试

Core：

- 两兄弟编组：新 Group、同父、单位 transform、子 `parentId`、Group 下标高于子层、undo/redo
- 单层可编组；不同父 no-op；选中 Group+子只包 Group（子被去掉）
- 解单位 Group：子回到原父、画面矩阵不变、Group 删除
- 解平移/旋转过的 Group：静态子层 position/rotation 已 bake；有关键帧的属性未改
- `SetParentCommand` undo

Bridge：`ms_command_group_layers` 返回 id；解组 true。

App：`TimelineReorder` 子孙扩展；缩进量随 depth。

---

## 验收

1. 选两层 `⌘G`：时间轴出现缩进的 Group 树；画布位置不变；一次 Undo 还原。
2. 选 Group `⇧⌘G`：子层回到原层级；若 Group 被移动过，静态子层留在世界位置。
3. SVG 导入的嵌套 Group 自动缩进。
4. 拖 Group 整棵子树一起走；只拖子层不改 parent。
