# SVG mask → Track Matte + Group PAG 包装 — 设计说明

日期：2026-08-20  
状态：已确认  
关联：[SVG 导入](./2026-08-19-svg-import-design.md) §7.5.1、[PAG 导出](./2026-07-31-pag-export-design.md) §3.5、[Image/Group 圆角](./2026-08-19-image-group-corner-radius-design.md) §3 PAG

## 目标

1. SVG `mask=` 导入成 **track matte**（均匀圆角 rect 除外，仍写图层 `cornerRadius`）。
2. PAG 导出时，**有 path mask 或自身是 track matte 目标** 的 Group 包进 export-only Precomp，让裁切作用在合成结果上（PAG `NullLayer` 裁不到子层）。

根因：`<g mask="url(#mask0_129_143)">` 现在写成 Group 的 `Layer.masks`。编辑器 isolation 能裁子层；导出成 `NullLayer` 后 mask 丢失。

## 已锁定决策

| 项 | 选择 |
|---|---|
| 均匀圆角 `mask`（`rx==ry` 且 `rx>0`） | 仍 `TryApplyUniformRoundedRectClip` → Group/Image `cornerRadius`，不建 matte 层 |
| 其余可导入 `mask=` | 同级 Shape 作 matte 源；目标层 `trackMatteLayerId` + `trackMatteType`，**不**写 `Layer.masks` |
| `mask-type` | `svgMask.getMaskType()`：`Alpha` → `TrackMatteType::Alpha`；默认/Luminance → `Luma` |
| `clip-path` | **不变**：圆角 → `cornerRadius`，其余 → path mask |
| 复杂 mask（多子 / `use` / `g` / `objectBoundingBox`） | 仍跳过 + `mask.skipped` |
| matte 源位置 | **目标层的同级**（`parentId = target.parentId`），不是目标的子层 |
| PAG Group isolation | 有 `cornerRadius` **或** 非空 `masks` **或** `trackMatteType ≠ None` → 包一层 Precomp；已有 mask / trackMatte 挪到宿主 `PreComposeLayer` |
| 圆角 PAG warning | 仅半径触发时仍报 `GroupCornerRadiusApproximated`；纯 mask / matte 包装不额外 warning |
| Core / schema / bridge | 不改模型；复用已有 track matte |

## 非目标

- `clip-path` 改成 track matte
- 复杂 mask（多形状 / 图片 / 滤镜）建成 Group matte 源
- 升 schema、新 Bridge API、App Inspector 改动
- 把已有工程里的 Group path mask 自动改写成 track matte（只改导入路径）

---

## §1 导入

`ApplyMask`（`src/import/svg/SvgWalk.cpp`）：

```
if 无法解析 / 非 userSpaceOnUse / 非单形状:
    mask.skipped
    return
if TryApplyUniformRoundedRectClip(target, shape):
    return
matte = 从 shape 建 Shape 层（几何同 AddShapeLayer：均匀圆角 rect 不会走到这里）
matte.parentId = target.parentId
matte.name = mask 节点 id，否则 shape id，否则 "Mask"
fill 用 mask 子形状自己的 paint（Luma 需要灰度）
ApplyNodeTransform(matte, shape)   // 不要对 <mask> 容器；不要 ApplyNodeEffects
target.trackMatteLayerId = matte.id
target.trackMatteType = mask-type Alpha ? Alpha : Luma
tree.layers.push(matte)
```

`BuildSvgLayers` 之后的中心锚点后处理照常扫到 matte 层。`ImportSvgInto` 给所有层写 `inPoint/outPoint`。求值侧 `usedAsMatteOnly` 已有，matte 源不上屏。

PAG 邻接由导出重排（`layers[index-1]`），导入层表顺序不要求紧邻。

`test.svg` 的 `mask0_129_143`：`style="mask-type:alpha"`（tgfx `SetStyleAttributes` 会拆成 `mask-type`）→ 匿名 Group 的 `trackMatteType = Alpha`，旁边 Shape（path = `bg_mask`）。

---

## §2 PAG 导出

`NullLayer` 的 mask / track matte 都不裁子层。把 `applyGroupCornerRadiusClip` 扩成 **isolation wrap**（可改名，逻辑合一，避免同一 Group 包两次）：

```
needsWrap(group):
    cornerRadius(inPoint) > 0
    || !group.masks.empty()
    || group.trackMatteType != None

wrapIsolatingGroup(host, group):
    子树（NullLayer + 子孙 + stroke siblings）→ nested VectorComposition
    宿主 PreComposeLayer 接手 transform / blend / effects / masks / trackMatte
    内层 NullLayer 改单位 transform，清 mask / trackMatte
    if radius > 0: 宿主再追加 AABB 圆角 mask（现有行为）+ GroupCornerRadiusApproximated
    host 里原 NullLayer 槽位换成 wrap
    其它层若 trackMatteLayer == 原 NullLayer → 改指向 wrap
```

matte **源** 不是目标子孙，留在外层。现有「源紧邻目标之上 + `isActive=false`」在 wrap **之前** 已排好；wrap 原位替换 NullLayer，邻接保持。

`applyGroupTrackMatteSourceWrap` 仍在 isolation wrap **之后**：Group 作 matte **源** 时把源子树再包一层（已有）。源已因 isolation 进嵌套合成则跳过（现有 `ContainsPagLayer` 判断）。

---

## §3 测试

| 范围 | 断言 |
|---|---|
| SvgImporter `MaskAttributeBecomesMask` | 目标 `trackMatteType ≠ None`，`masks.empty()`，存在同级 matte Shape |
| `MaskOnGroupBecomesMask` | 同上，目标是 Group |
| 均匀圆角 mask | 仍 `cornerRadius`，无 track matte |
| `mask-type="alpha"` / `style="mask-type:alpha"` | `TrackMatteType::Alpha` |
| 默认 / luminance | `TrackMatteType::Luma` |
| `ComplexMaskAttributeIsSkipped` | 仍 `mask.skipped`，无 matte |
| clip-path 单 path | 仍 `Layer.masks`，不是 track matte |
| PagExporter Group + path mask | 宿主是 PreComposeLayer 且带 mask；内层无该 mask |
| PagExporter Group + track matte 目标 | 宿主 PreComposeLayer 带 trackMatte；matte 源 `isActive=false` 且紧邻之上 |

不新开视觉金图。`test.svg` 作人工核对即可。

---

## §4 文档

- 本 spec。
- 修订 [SVG 导入](./2026-08-19-svg-import-design.md) 已锁定表与 §7.5.1。
- 修订 [Image/Group 圆角](./2026-08-19-image-group-corner-radius-design.md) PAG：「用户 mask / track matte 仍走现有导出」改为 isolation wrap。
- `docs/README.md` 索引一行。
