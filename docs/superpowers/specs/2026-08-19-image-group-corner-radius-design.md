# Image / Group 圆角 — 设计说明

日期：2026-08-19  
状态：已确认，待实现  
分支：`feature/svg_importer`

## 目标

1. Image 层按容器 `size` 裁成圆角矩形。
2. Group 层按**子层包围盒**裁成圆角矩形；有圆角 / mask / track matte 时整组离屏合成后再裁/遮罩。
3. 圆角可关键帧，Inspector 与时间轴和 Shape Radius 一致。

## 已锁定决策

| 项 | 选择 |
|---|---|
| 作用对象 | Image **和** Group |
| Group 裁剪框 | 子孙几何在 Group 局部空间的 AABB（`BoundsOfDescendantUnionLocal`） |
| Group 绘制 | 有 isolation 才 `BeginLayer`；无圆角且无 mask/matte 则子层直画 |
| Isolation 触发 | `(cornerRadius>0 && 有 AABB)` **或** 非空 masks **或** track matte ≠ None |
| 圆角存储 | `Animatable<float>`，默认 0 |
| 实现 | 独立属性 + 硬 `ClipPath`（不用隐藏 mask） |
| Group content | 继续用 `NullContent`，不新建 `GroupContent` |
| undo | 已有 `SetStaticFloat` / 关键帧，不新开命令 |
| schema | 不升版本；缺字段 = 0 |

## 非目标

- 四角独立半径
- Group 自带 Width / Height
- 圆角羽化
- 升 schema
- 为 Group 新做 effects / layerStyles UI（若 isolation 已发生，`EndLayer` 照常带上已求值的 effects/styles）

---

## §1 数据模型

```cpp
class ImageContent : public LayerContent {
    EntityId assetId;
    Animatable<Vec2> size{Vec2{200, 200}};
    Animatable<float> cornerRadius{0.0f};  // 新增
    ImageScaleMode scaleMode = ImageScaleMode::LetterBox;
};

class NullContent : public LayerContent {  // LayerType::Group
    Animatable<float> cornerRadius{0.0f};  // 新增
};
```

属性路径：

| 层 | 路径 |
|---|---|
| Image | `image.cornerRadius`（与 `image.size` 并列） |
| Group | `content.cornerRadius`（`ResolveAnimatable`：`content.*` 现只认 Text；Group 增这一条） |

序列化：Image `content` 写 `cornerRadius`；Group `content` 从空对象变为可带 `cornerRadius`。缺省兼容，不升 schema。

求值 clamp：`≥ 0`，上限为裁剪框短边的一半（Image = `size`；Group = 子层 AABB）。

`MakeGroupLayersCommand` 不改：新建 Group 半径为 0。

---

## §2 求值 / 绘制

`EvaluatedImageItem` 与 `EvaluatedLayer` 各加 `float cornerRadius`（已 clamp）。

删除 `InheritAncestorTrackMattes`。Group 的 track matte 作用在整组 isolation 上，不再抄到每个子层。

**AABB**：复用 `BoundsOfDescendantUnionLocal`。并集含 Image 容器、Shape 路径（含描边）、Text box。不含 effect 外扩。只有圆角、没有包围盒 → 不 isolation。

### Image（半径单独不额外 BeginLayer）

```
Save + Concat(world) + opacity/blend
  [已有 mask/effect/styles 才 BeginLayer]
  if radius > 0: ClipPath(roundedRect(0, 0, size))
  DrawImage
  masks / track matte / EndLayer
Restore
```

`ClipPath` 必须在 `DrawImage` 之前（canvas clip 只影响后续绘制）。

### Group isolation

`CommandBuilder` 改为按树包 Group：在 `layers[]` 碰到 isolation Group 时发出包裹，其子孙在扁平循环里跳过（由该 Group 发出）。嵌套 isolation Group 在父 isolation 内再包一层。

整组在 **Group 自己的 `layers[]` 下标** 合成。子层和 Group 之间若夹了别的层，子层会跟着 Group 走（常见「子层紧挨、Group 在后」的块不受影响）。

```
needsIsolation =
    (radius > 0 && hasAABB) || !masks.empty() || trackMatte != None

Save
Concat(group.world)
SetOpacity(group) / SetBlend(group)   // beginLayer 快照，合成 isolation 时用
BeginLayer
  ClipPath(roundedAABB)               // 硬圆角；在子层之前
  for child in descendants:
      嵌套 isolation Group → 再 emitGroupIsolation
      否则:
        Save
        Concat(inverse(group.world) * child.world)
        SetOpacity(child.opacity / group.opacity)  // 避免 Group 透明度乘两次
        emit leaf
        Restore
  if masks: BeginMask … EndMask
  if trackMatte: BeginMask(matte) … EndMask
EndLayer
Restore
```

`child.opacity` 已含祖先。isolation 内用 `child.opacity / group.opacity`，这里的 group 是**正在进入的** isolation Group（嵌套时除以最近这一层）。`group.opacity` 过小则当 0，不画。Group 的 opacity / blend 只作用在整张离屏图上。

圆角不是用户 mask。硬 `ClipPath` 与「先合成再硬裁」视觉等价。用户 mask / track matte 仍按现有顺序：内容之后套。

### 点选

- Image：命中测试改为圆角矩形（容器内、圆角外不算）。
- 子层若有圆角 isolation 祖先：点还要落在该祖先的圆角 AABB 内，否则不命中。

---

## §3 Inspector / 时间轴 / 导出

**Inspector**

- Image：`ImageLayerInspector` 在 Height 下加 Radius 行（`NumberPropertyRow` + 关键帧），路径 `image.cornerRadius`。
- Group：Transform 前加 `Group` 小节，一行 Radius，路径 `content.cornerRadius`。
- `ImageProperty` 增加 `cornerRadius`。不新开 Bridge API。
- Inspector 把 `< 0` 写成 0；短边一半的上限只在求值时 clamp。

**时间轴**

`timelineAnimatedPropertyPaths` 增加 `image.cornerRadius` 与 `content.cornerRadius`。有关键帧才出轨，标签 `Corner Radius`。

**PAG**

- Image：容器空间加圆角矩形 mask（接到 `applyImageContainerFit` 的裁剪 mask）。半径有关键帧 → 用 in-point 烘焙，warning `ImageCornerRadiusAnimationBaked`。
- Group：PAG `NullLayer` 的 mask 裁不到子层。`radius > 0` 时把子树包进 Precomp，用 in-point 的 AABB + 半径做圆角 mask，warning `GroupCornerRadiusApproximated`。
- Group 上已有的用户 mask / track matte 仍走现有导出。

**MP4 / 预览**

走 `Evaluate → BuildCommands`，圆角直接生效。不把 Image/Group 圆角强制清 0（合成圆角在 MP4 里清 0 的规则不变）。

---

## §4 测试

Core 单测走 ASan。不新开视觉金图，除非现有 adapter 测试能顺手接上。

| 范围 | 断言 |
|---|---|
| PropertyPath | `image.cornerRadius` / `content.cornerRadius` 能解析；Shape/Text 不误伤 |
| Serializer | Image/Group 圆角 round-trip；旧文件缺字段 → 0 |
| SceneEvaluator | Image/Group 求出 clamp 后的 radius；无圆角/mask/matte 的 Group 不 isolation |
| CommandBuilder | Image `radius>0`：`ClipPath` 在 `DrawImage` 前，无额外 `BeginLayer`；Group 有圆角：`BeginLayer` 包子孙，`ClipPath` 在子绘制前，子层用相对变换；无圆角的 Group 不发 `BeginLayer` |
| Track matte | Group 上的 matte 打在 isolation 上，子层不再继承 |
| HitTest | Image 圆角外不命中；Group 圆角 AABB 外的子层不命中 |
| PagExporter | Image 带圆角 mask；动画半径 warning `ImageCornerRadiusAnimationBaked`；Group 圆角 warning `GroupCornerRadiusApproximated` |

App：Image/Group Inspector 的 Radius 行能读写关键帧（按现有 Inspector 测试风格，有才加）。
