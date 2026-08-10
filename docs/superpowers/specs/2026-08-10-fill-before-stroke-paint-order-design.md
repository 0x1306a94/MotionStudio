# Fill-before-Stroke 固定合成顺序 — 设计说明

日期：2026-08-10  
状态：已实现  
范围：Shape / Text 的 `Layer::styles` 求值绘制顺序；不改序列化 schema

相关：`ApplyLayerStyles` / `SceneEvaluator`（Text styles）/ Inside Stroke 被后画 Fill 盖住

## 目标

1. 无论 `styles[]` 里 Fill / Stroke 如何交错，绘制始终 **先全部 Fill，再全部 Stroke**
2. 同类内部仍按 `styles[]` 出现顺序（稳定）
3. 不改文档格式、`styles[i]` PropertyPath、undo 索引语义

## 非目标

- 拆成 `fills[]` + `strokes[]`
- Inspector 跨类型拖拽改序
- 新增 `placement` / Foreground Fill 等扩展叠法
- 迁移改写已有 `document.json` 里的 `styles` 数组顺序（求值层修正即可）

## 已锁定决策

| 项 | 选择 |
|---|---|
| 方案 | A：固定合成顺序 Fill → Stroke |
| 存储 | 保留单一 `Layer::styles` |
| 改序位置 | 求值时（`SceneEvaluator`），不改磁盘 |
| 同类保序 | 扫描 `styles[]` 时各自稳定保序 |
| 插入策略 | `AddLayerStyle` 时 Fill 插到首个 Stroke 前、Stroke 追加末尾（磁盘更直观；正确性仍由求值保证） |
| Text | 与 Shape 同一规则（先 fill paints，再 stroke paints） |
| PAG 导出 | Shape 本就 Fill/Center-Stroke 上主层、定位 Stroke 平行层；Text 已 `strokeOverFill=true`。本次不强制改 export，仅保证预览与导出语义一致（Stroke 在 Fill 上） |

---

## §1 行为

| `styles[]` 磁盘顺序 | 绘制结果 |
|---|---|
| `[Fill, Stroke]` | Fill → Stroke（与今相同） |
| `[Stroke, Fill]`（Pen 默认 Stroke 后再加 Fill） | **仍** Fill → Stroke（修复点） |
| `[FillA, Stroke, FillB]` | FillA → FillB → Stroke |
| `[StrokeA, Fill, StrokeB]` | Fill → StrokeA → StrokeB |

Inside / Outside / Center Stroke 均受益：Fill 不再盖住后添加的 Stroke。

开放路径无 fill faces 时 Inside 仍可能几何为空（既有算法限制）；本次不处理。

## §2 实现要点

### Shape：`ApplyLayerStyles`

伪代码：

```
items = []
for style in layer.styles where Fill:
    append fill items
for style in layer.styles where Stroke:
    append stroke items
```

两遍扫描即可，无需重排 `layer.styles`。

### Text：`SceneEvaluator` 文本分支

当前单遍 `push_back` 与 `styles[]` 顺序绑定；改为同样两遍（或先收集再拼），保证 `textItem.styles` 先 fill 后 stroke。

### 可选：`AddLayerStyleCommand`

- 添加 Fill：插入到第一个 Stroke 之前（若无 Stroke 则 `push_back`）
- 添加 Stroke：`push_back`
- undo 仍按 style `id` 移除（已有逻辑），不依赖「总是末尾」

不强制归一化已有文档；旧文件靠求值顺序修复。

### 文档

更新 `docs/data-model.md` / `docs/rendering.md`：明确「绘制合成 = Fill 块 → Stroke 块；`styles[]` 顺序仅约束同类内部」。

## §3 测试

1. **SceneEvaluator**：构造 `[Stroke, Fill]`（Stroke 为 Inside），断言 `shapeItems` 顺序为 Fill 项在前、Stroke 项在后，且 `stroke.position == Inside`
2. **SceneEvaluator Text**（若有现成夹具）：`[Stroke, Fill]` → `textItem.styles` 先非 stroke 再 stroke
3. 回归：既有 `[Fill, Stroke]` 用例顺序不变

## §4 风险与边界

| 风险 | 处理 |
|---|---|
| 有人依赖「Fill 盖 Stroke」做特效 | 本次明确放弃；以后若要再加 placement |
| PropertyPath `styles[i]` 与绘制序不一致 | 接受：索引仍指磁盘数组；UI 已分 Fills/Strokes 栏 |
| PAG 导出仍按数组遍历 | Text 取「第一个 Fill / 第一个 Stroke」本就与顺序无关叠法；Shape Inside 为平行层，不受主层 elements 序影响 |

---

## 验收

- Pen 层默认 Stroke 后再加黑色 Fill，将 Stroke Position 设为 Inside：画布上可见描边
- `styles` 磁盘仍可为 `[stroke, fill]`，无需用户手动重排
- 不改 `.motionproject` schema 版本
