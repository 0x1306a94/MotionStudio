# Style Stack Order（Fills / Strokes 列表序）— 设计说明

日期：2026-08-11  
状态：待实现  
关联：[数据模型](../../data-model.md)、[Gradient Paint](2026-08-11-gradient-paint-design.md)

## 目标

对齐 Figma 的 **「列表最上 = 绘制最顶」** 观感，并支持在同类 style 内调整顺序：

1. Inspector 倒序展示 Fills / Strokes（新添加的出现在列表顶部）
2. 每行提供上移 / 下移按钮（相对视觉列表）
3. Core 绘制序与 `styles[i]` 语义保持不变

## 已锁定决策

| 项 | 选择 |
|---|---|
| 数据序 | **不变**：`styles[]` index 小 = 先画（下），大 = 后画（上） |
| Fill / Stroke 分区 | **不变**：fills 整块仍在 strokes 之前（添加与求值已如此） |
| 添加 | **不变**：Fill 插在第一个 Stroke 前（fills 末尾）；Stroke append → 新项绘制在同类最顶，倒序 UI 后出现在列表顶 |
| UI 列表方向 | Fills / Strokes **倒序显示** |
| 调序交互 | **上移 / 下移按钮**（不做拖拽） |
| 调序范围 | 仅同类连续区间内（Fill↔Fill 或 Stroke↔Stroke）；禁止跨类型 |

## 非目标

- 列表拖拽排序
- 翻转 Core / 序列化 / PAG 的 paint 序语义
- 允许 Fill 画在 Stroke 之上（保持现有「先全部 Fill 再全部 Stroke」求值策略）
- 合并 Fill+Stroke 为单一可混排列表

## 方案取舍

选用 **UI 倒序 + `MoveLayerStyleCommand`**：

- 不破坏既有文档、`styles[i]` PropertyPath、导出与测试假设
- 新 Fill 已 append 到 fills 末尾 → 已是绘制最顶；只需倒序展示即可贴近 Figma「添加在前」

不采用：翻转 Core 数组语义（迁移与路径成本高）；仅倒序展示不提供调序（无法纠正叠盖关系）。

---

## 语义

### 绘制（Core，不变）

```
styles: [Fill0, Fill1, ..., Stroke0, Stroke1, ...]
paint:  Fill0 → Fill1 → … → Stroke0 → Stroke1 → …
        (底)                         (顶)
```

`SceneEvaluator` 现有两趟循环（先 fills 再 strokes）保持不变。

### UI 列表

```
显示序（上→下）= 同类 index 从大到小
例：fills indices [0,1,2] → UI 行顺序 2, 1, 0
```

上移 / 下移相对**视觉列表**：

| 按钮 | 视觉效果 | Core 操作 |
|---|---|---|
| 上移 | 该行更靠近列表顶（更「上」） | 与**更大** index 的同类邻居交换（或移向更大 index） |
| 下移 | 该行更靠近列表底 | 与**更小** index 的同类邻居交换 |

视觉最上行：上移禁用；视觉最下行：下移禁用。

---

## Core API

### `MoveLayerStyleCommand`

```cpp
class MoveLayerStyleCommand : public Command {
  // layerId, fromIndex, toIndex（均为 styles[] 下标）
  // execute: 若 from/to 越界、同类不同、或跨越 Fill/Stroke 边界 → no-op
  // 合法时 erase+insert（或相邻 swap 链），保留 style 身份与全部字段
};
```

校验伪代码：

```
sameType(from, to)
from 与 to 之间（含端点）不得出现另一类型   // 保证落在同类连续块内
```

`CommandKind::MoveLayerStyle` 新增。

### Bridge / App

```c
void ms_command_move_layer_style(MSDocument *document, uint64_t layerId,
                                 int fromIndex, int toIndex);
```

```swift
func moveStyle(layerID: UInt64, from fromIndex: Int, to toIndex: Int)
```

Inspector：`perform("Move Fill" / "Move Stroke") { core.moveStyle(...) }`。

---

## UI 改动

`FillsInspector` / `StrokesInspector`：

1. `fillIndices()` / `strokeIndices()` 结果 **reversed** 后再 `ForEach`
2. 每行增加上移 / 下移（`chevron.up` / `chevron.down` 或等价）
3. 计算视觉位置：`visualIndex` 0 = 顶；映射 `coreIndex`；上移目标 = 同类中下一个更大的 core index，下移反之
4. 「Fill N」标签：按**视觉序**编号（顶为 1）或保持 core index+1——**采用视觉序**（顶 = Fill 1），与 Figma「从上往下数」一致

---

## 测试

- `MoveLayerStyleCommand`：同类型相邻/跨多格；跨 Fill↔Stroke no-op；undo/redo 还原顺序与 id
- 可选 bridge 冒烟：`ms_command_move_layer_style`
- UI 无强制截图门禁；手动：添加两 Fill → 新在顶；上移/下移改变叠盖

## 文档

实现时同步 `docs/data-model.md`：注明 Inspector 倒序展示与 `MoveLayerStyleCommand`。

## 实现顺序建议

1. Core command + 单测  
2. Bridge + App `moveStyle`  
3. Inspector 倒序 + 按钮  
4. data-model 一句补丁  
