# Layer Styles（Drop Shadow + Outer Glow + Stroke）— 设计说明

日期：2026-08-16  
状态：已实现  
前置：[Layer Effects](2026-08-16-layer-effects-design.md)、[Layer Effects UI](2026-08-16-layer-effects-ui-design.md)、[PAG RuntimeFilter](2026-08-13-pag-runtime-filter-design.md)  
相关：[rendering.md](../../rendering.md)、[data-model.md](../../data-model.md)、PAG `DropShadowFilter` / `OuterGlowFilter` / `StrokeFilter` / `SolidStrokeFilter`

## 目标

移植 PAG `layerstyle/` 目录里 **SolidStroke 家族** 的 Layer Style：用图层结果图的 alpha 生成阴影、外发光或描边，按 Behind / Above 叠回。

1. Core：`Layer::layerStyles[]`，与 `styles[]`（Fill/Stroke）和 `effects[]`（后处理）并列。
2. 求值进 `SceneState`，`EndLayer` 同时带 effects 与 layerStyles 快照。
3. adapter：effect 链之后按 PAG 公式画 Drop Shadow / Outer Glow / Stroke。
4. Inspector + 时间轴：可增删改、打关键帧。

## 非目标

- Gradient Overlay（渐变模型 + 另一条绘制路径，另开 spec）
- Inner Shadow / Inner Glow / Bevel（PAG 此目录没有 GPU 实现）
- Outer Glow 的 AE 扩展项：`noise`、渐变色、`technique`、`jitter`（PAG GPU 滤镜也不用）
- 把 Layer Style 放进 `effects[]` 或现有 `styles[]`
- Precomp / Group 组级 isolation（挂在它们上的 layer style **静默忽略**）
- Lottie / PAG 导出写入 Layer Style 块
- 命中 / 选中框随装饰扩张
- 升 `schemaVersion`

## 已锁定决策

| 项 | 选择 |
|---|---|
| 挂载 | 新桶 `layerStyles[]`，基类 `LayerFx`（不复用 `LayerStyle` / `MS_STYLE`） |
| 作用对象 | 仅 Shape / Image / Text 叶子层 |
| 相对 effect | **先 effect，再 layer style** |
| 相对 mask | 先 mask / track matte，再 effect，再 layer style |
| 绘制顺序 | **Behind 装饰 → 原图 → Above 装饰**（同侧保持数组序） |
| 离屏 | 复用 `BeginLayer` / `EndLayer`；有 layerStyles 或 effects 或 mask 才离屏 |
| 多条 | 数组序：先画的在下面；添加 = append |
| 首版类型 | `DropShadow`、`OuterGlow`、`Stroke`（类名 `LayerStrokeStyle`，避免和路径 `StrokeStyle` 撞名） |
| DropShadow 参数 | `blendMode` `color` `opacity` `angle` `distance` `size` `spread` |
| OuterGlow 参数 | `blendMode` `color` `opacity` `size` `spread` `range` |
| Stroke 参数 | `blendMode` `color` `opacity` `size` `position`（复用 `StrokePosition`） |
| `blendMode` / Stroke `position` | 静态，专用命令，不上时间轴 |
| 默认 blend | Shadow `Multiply`；Glow `Screen`；Stroke `Normal` |
| 可动画 | Shadow：`color` `opacity` `angle` `distance` `size` `spread`；Glow：`color` `opacity` `size` `spread` `range`；Stroke：`color` `opacity` `size` |
| `opacity` | 0～1 |
| `spread` / `range` | 0～1；`range` 下限 0.01 |
| `color.alpha` | 绘制忽略，透明度只用 `opacity` |
| Shadow 默认 | 黑 / 0.75 / 135° / distance 5 / size 5 / spread 0 |
| Glow 默认 | RGB(1, 1, 0.745) / 0.75 / size 5 / spread 0 / range 1 |
| Stroke 默认 | 红 / 1.0 / size 3 / `Outside` |
| 恒等 | 见 §1 snapshot |
| 未知 type | 反序列化跳过该项 |
| schema | 不升版；`layerStyles` 缺省 = `[]` |
| 求值快照 | `LayerFx::snapshot(time)` → `shared_ptr<const LayerFx>` |
| 分发 | `type()` + `static_cast`；禁 `dynamic_cast` |
| 命中 / outline | 不随 style 改 |
| UI | Inspector + 时间轴一起做 |

---

## §1 数据模型

`styles[]` 仍是 Fill/Stroke。`effects[]` 仍是 Image→Image 后处理。`layerStyles[]` 是对**整层结果图**的装饰。

```cpp
enum class LayerFxType {
    DropShadow,
    OuterGlow,
    Stroke,
};

enum class LayerFxDrawPosition {
    Behind,
    Above,
};

class LayerFx {
  public:
    explicit LayerFx(LayerFxType type);
    virtual ~LayerFx() = default;
    LayerFxType type() const;
    virtual LayerFxDrawPosition drawPosition() const;  // 默认 Behind

    EntityId id = EntityId::Generate();
    bool enabled = true;

    virtual std::shared_ptr<const LayerFx> snapshot(PreviewTime time) const = 0;

  private:
    LayerFxType type_;
};

class DropShadowStyle : public LayerFx { /* blendMode Multiply; color/opacity/angle/distance/size/spread */ };
class OuterGlowStyle : public LayerFx { /* blendMode Screen; color/opacity/size/spread/range */ };

class LayerStrokeStyle : public LayerFx {
  public:
    LayerStrokeStyle();  // type Stroke；drawPosition() → Above
    BlendMode blendMode = BlendMode::Normal;
    Animatable<Color> color{Color{1, 0, 0, 1}};
    Animatable<float> opacity{1.f};
    Animatable<float> size{3.f};
    StrokePosition position = StrokePosition::Outside;
    std::shared_ptr<const LayerFx> snapshot(PreviewTime time) const override;
};

class Layer {
    std::vector<std::unique_ptr<LayerStyle>> styles;
    std::vector<std::unique_ptr<LayerEffect>> effects;
    std::vector<std::unique_ptr<LayerFx>> layerStyles;
};
```

不进 `EntityIndex`。命令持 `layerId` + index。Precomp / Group 上可持久化，求值与绘制不读。

### snapshot 恒等

`size` / `distance` / `spread` / `range`：`<0` 当成 `0`。`spread` clamp 到 `[0, 1]`。`range` clamp 到 `[0.01, 1]`。

丢掉：

- `!enabled`
- `opacity <= 0`
- DropShadow：`size<=0 && distance<=0 && spread<=0`
- OuterGlow：`size<=0`
- Stroke：`size<=0`

### 序列化 / PropertyPath

`document.json` 图层可选字段 `layerStyles`。缺省或非数组 → 空。`schemaVersion` 保持 1。

```json
"layerStyles": [
  {
    "type": "dropShadow",
    "blendMode": "multiply",
    "color": { "static": [0, 0, 0, 1] },
    "opacity": { "static": 0.75 },
    "angle": { "static": 135 },
    "distance": { "static": 5 },
    "size": { "static": 5 },
    "spread": { "static": 0 }
  },
  {
    "type": "outerGlow",
    "blendMode": "screen",
    "color": { "static": [1, 1, 0.745, 1] },
    "opacity": { "static": 0.75 },
    "size": { "static": 5 },
    "spread": { "static": 0 },
    "range": { "static": 1 }
  },
  {
    "type": "stroke",
    "blendMode": "normal",
    "color": { "static": [1, 0, 0, 1] },
    "opacity": { "static": 1 },
    "size": { "static": 3 },
    "position": "outside"
  }
]
```

- `type`：`dropShadow` / `outerGlow` / `stroke`；未知 → 跳过该项。
- `enabled` 缺省 `true`。
- `blendMode` 缺省按类型：multiply / screen / normal。
- Stroke `position`：`center` / `inside` / `outside`；缺省 `outside`。

PropertyPath：

| 类型 | 路径 |
|---|---|
| 三者 | `layerStyles[i].color` `.opacity` `.size` |
| DropShadow | `.angle` `.distance` `.spread` |
| OuterGlow | `.spread` `.range` |

`enabled` / `blendMode` / Stroke `position` 用专用小命令。

---

## §2 求值与 DrawCommand

`EvaluatedLayer` 增加 `std::vector<std::shared_ptr<const LayerFx>> layerStyles`。

`FillCommonLayerFields` 在 effects 求值之后对 `layer.layerStyles` 做 `snapshot`。

```
needsIsolation =
    !evaluated.masks.empty()
    || evaluated.trackMatteType != None
    || !evaluated.effects.empty()
    || !evaluated.layerStyles.empty()
```

```cpp
struct DrawCommand {
    std::vector<std::shared_ptr<const LayerEffect>> effects;
    std::vector<std::shared_ptr<const LayerFx>> layerStyles;
};

class RenderAdapter {
    virtual void endLayer(
        const std::vector<std::shared_ptr<const LayerEffect>> &effects,
        const std::vector<std::shared_ptr<const LayerFx>> &layerStyles) = 0;
};
```

`PlayCommands`：`adapter.endLayer(command.effects, command.layerStyles)`。实现方目前只有 `TgfxCanvasAdapter`。

---

## §3 适配器

`endLayer`：

```
image = PictureToImage(...)          // 仅 mask、无 effect、无 style 时仍走 drawPicture
image = Apply effects[]
offsetAccum = effect 链的 bounds 偏移

composited = 新离屏（包住 Behind 扩张 + 原图 + Above）
for style in layerStyles where drawPosition == Behind:
    画装饰到 composited（alpha = style.opacity，blend = style.blendMode）
composited.draw(image)（Normal / alpha 1）
for style in layerStyles where drawPosition == Above:
    画装饰到 composited（同上）

parent.draw(composited) 于 contentBounds.origin + offsetAccum
paint 用图层 opacity / blend
```

仅有 layerStyles、没有 effects 时也必须 `PictureToImage`。没有任何 layer style 时不必多一张 composited，直接 `parent.draw(image)`。

单条 style 失败：跳过该条，继续后续 style 与原图。

图层 opacity 必须作用到阴影和描边，所以装饰不能直接画到 parent。

### Drop Shadow（对齐 PAG `DropShadowFilter`）

```
radians = DegreesToRadians(angle - 180)
offsetX = cos(radians) * distance
offsetY = -sin(radians) * distance
spreadC = clamp(spread, 0, 1)

spreadC == 0:
    filter = DropShadowOnly(offsetX, offsetY, size/2, size/2, color)
spreadC == 1:
    filter = SolidStroke(size, size, offsetX, offsetY, color)   // position Invalid
0 < spreadC < 1:
    filter = Compose(
        SolidStroke(size*spreadC, …, offset, color),
        DropShadowOnly(0, 0, size*(1-spreadC)/2, …, color))

thick 当 size*spreadC >= 12
```

### Outer Glow（对齐 PAG `OuterGlowFilter`）

无位移。`rangeC = clamp(range, 0.01, 1)`。

```
spreadC == 0:
    filter = DropShadowOnly(0, 0, size*(1-spreadC)/rangeC/2, …, color)
spreadC == 1:
    filter = SolidStroke(spreadC*size/rangeC, …, offset 0, color)
0 < spreadC < 1:
    filter = Compose(SolidStroke(spreadC*size/rangeC, …), DropShadowOnly(0, 0, size*(1-spreadC)/rangeC/2, …))

thick 当 size >= 12
```

### Stroke（对齐 PAG `StrokeFilter`）

`drawPosition == Above`。thick 当 `size >= 12`。

PAG 把 Inside/Center 的 spread 缩小：Center `*0.4`，Inside `*0.8`，Outside 不改。照抄。

```
option.color / spreadSize / position = 上表
Outside:
    filter = SolidStroke(option, mode, original=null)
Inside / Center:
    filter = Compose(AlphaEdgeDetect, SolidStroke(option, mode, original=image))
```

`StrokePosition` 用我们的枚举（Center/Inside/Outside），不要用 PAG 的数值序。

### 画一条装饰

```
decorated = image->makeWithFilter(filter, &point)
compositeCanvas.draw(decorated, point, alpha=style.opacity, blend=style.blendMode)
```

spread 尺寸 clamp 到 25。`STROKE_SPREAD_MIN_THICK_SIZE` = 12。

### SolidStrokeFilter / AlphaEdgeDetectFilter

`adapter/tgfx/src/effects/SolidStrokeFilter.{h,cpp}`：移植 PAG Normal / Thick shader、`filterBounds`、顶点扩张。

- DropShadow / Glow：`position` Invalid，不带 original。
- Stroke：传入 `position`；Inside/Center 带 original image。
- `RuntimeFilter` + `acquireUniformSlice`。
- Normal / Thick 各一个 `filterType()`。

`adapter/tgfx/src/effects/AlphaEdgeDetectFilter.{h,cpp}`：移植 PAG shader，仅 Stroke Inside/Center 使用。

---

## §4 Undo / Bridge / UI

### Undo

| 命令 | 行为 |
|---|---|
| `AddLayerFxCommand` | append 指定类型；undo 按 `id` 移除 |
| `RemoveLayerFxCommand` | 按下标移除并持有对象；undo 插回原下标 |
| `MoveLayerFxCommand` | `fromIndex` → `toIndex`；可 merge |
| `SetLayerFxEnabledCommand` | 改 `enabled` |
| `SetLayerFxBlendModeCommand` | 改 `blendMode` |
| `SetLayerFxStrokePositionCommand` | 改 Stroke `position`；非 Stroke 静默跳过 |

可动画参数走现有 `SetStaticValue` / 关键帧（含 `color`）。

### Bridge

```c
typedef CF_CLOSED_ENUM(int32_t, MS_LAYER_FX) {
    MS_LAYER_FX_INVALID = -1,
    MS_LAYER_FX_DROP_SHADOW = 0,
    MS_LAYER_FX_OUTER_GLOW = 1,
    MS_LAYER_FX_STROKE = 2,
};

int ms_layer_fx_count(MSDocument *, uint64_t layerId);
MS_LAYER_FX ms_layer_fx_type_at(MSDocument *, uint64_t layerId, int index);
bool ms_layer_fx_enabled_at(MSDocument *, uint64_t layerId, int index);
MS_BLEND ms_layer_fx_blend_mode_at(MSDocument *, uint64_t layerId, int index);
MS_STROKE_POSITION ms_layer_fx_stroke_position_at(MSDocument *, uint64_t layerId, int index);

void ms_command_add_drop_shadow(MSDocument *, uint64_t layerId);
void ms_command_add_outer_glow(MSDocument *, uint64_t layerId);
void ms_command_add_layer_stroke(MSDocument *, uint64_t layerId);
void ms_command_remove_layer_fx(MSDocument *, uint64_t layerId, int index);
void ms_command_move_layer_fx(MSDocument *, uint64_t layerId, int fromIndex, int toIndex);
void ms_command_set_layer_fx_enabled(MSDocument *, uint64_t layerId, int index, bool enabled);
void ms_command_set_layer_fx_blend_mode(MSDocument *, uint64_t layerId, int index, MS_BLEND mode);
void ms_command_set_layer_fx_stroke_position(MSDocument *, uint64_t layerId, int index,
                                             MS_STROKE_POSITION position);
```

复用已有 `MS_STROKE_POSITION`。label：`Drop Shadow` / `Outer Glow` / `Stroke`。

### Inspector

新建 `LayerStylesInspector.swift`，挂在 `EffectsInspector` **之后**。仅 Shape / Image / Text。

段头 `Layer Styles` + `+` 菜单（三项）。列表**倒序**。

每行：标题、enabled、上下移、删除；`ColorPicker`；按类型的 Number 行；Blend picker；Stroke 另加 Position picker（抄路径 Stroke）。

| 类型 | 数值行 |
|---|---|
| Drop Shadow | Opacity / Angle / Distance / Size / Spread |
| Outer Glow | Opacity / Size / Spread / Range |
| Stroke | Opacity / Size |

`perform`：`Add Drop Shadow` / `Add Outer Glow` / `Add Stroke` / `Remove Layer Style` / `Move Layer Style` / `Set Layer Style Enabled` / `Set Layer Style Blend Mode` / `Set Layer Style Position`。

### 时间轴

`timelineLayerStyleTracks` 挂进已有路径收集。仅叶子层；只追加已有关键帧。

| 路径 | 行 label |
|---|---|
| `.color` / `.opacity` / `.size` | `{Type} Color/Opacity/Size` |
| `.angle` / `.distance` | `Drop Shadow Angle/Distance` |
| `.spread` | `{Type} Spread`（Shadow / Glow） |
| `.range` | `Outer Glow Range` |

`{Type}` = `Drop Shadow` / `Outer Glow` / `Stroke`。`enabled` / `blendMode` / `position` 不上轴。

---

## §5 测试

### Core（`core_tests`）

| 用例 | 断言 |
|---|---|
| 求值跳过 | `!enabled`、`opacity<=0`、Shadow 三零、Glow/Stroke `size<=0` 不进快照 |
| 求值保留 | 三类默认值进快照；clamp 生效 |
| Precomp 忽略 | 预合成带 layerStyles 时子层无组级 isolation |
| CommandBuilder | 仅 layerStyles → 有 Begin/EndLayer，条数对 |
| effect + style | EndLayer 同时带两者 |
| 序列化 round-trip | 三类型全字段 + 关键帧；缺 `layerStyles` 的旧 JSON 仍能加载 |
| 未知 type | 该项跳过 |
| undo | 增删移、enabled、blendMode、stroke position；PropertyPath 改 distance / range / size / color |

### Adapter（`tgfx_adapter_test`）

走 `Evaluate → BuildCommands → PlayCommands`。

| 用例 | 断言 |
|---|---|
| Shadow 在内容后 | 色块外侧沿 angle/distance 出现偏黑非零像素 |
| Shadow 原图未染色 | 色块中心仍是填充色 |
| Glow 在四周 | 色块四周出现发光色，无单向偏移 |
| Stroke Outside | 色块外侧出现描边色 |
| Stroke 盖在内容上 | 描边与内容同时可见（Above，不是只替换内容） |
| spread | Shadow 或 Glow 的 `spread=1` 与 `0` 的 `readPixels` 不全等 |
| 图层 opacity | opacity 0.5 时装饰与内容一起变淡 |
| 与 blur 顺序 | Blur effect + DropShadow：渗出区也有影 |

不比黄金 PNG。ASan 无泄漏。无 Inspector / 时间轴自动化 UI 测试。

---

## §6 文档

实现时改：

- `docs/data-model.md`：`Layer::layerStyles[]`，与 `styles[]` / `effects[]` 职责表
- `docs/rendering.md`：isolation 条件、EndLayer、Behind → 原图 → Above
- `docs/pag-runtime-filter.md`：SolidStroke / AlphaEdgeDetect / 三种 style
- `docs/README.md`：本 spec 一行索引
- Layer Effects spec「后续」：本目录三类型指向本 spec；Gradient Overlay 仍不做

---

## §7 错误与边界

- `endLayer` 无 isolation / 无 surface：空操作。
- `PictureToImage` 失败：退回无滤镜、无 style 的 `drawPicture`。
- effect `Apply` 失败：停 effect 链，仍用当前 image 画 layer styles。
- 单条 layer style 失败：跳过该条。
- 禁止 `dynamic_cast` / 异常。
- Group / Precomp 上的 layerStyles：存盘保留，不渲染、不报错。

---

## 里程碑

1. 模型 + 序列化 + PropertyPath + undo（三类型）
2. SceneEvaluator / CommandBuilder / `endLayer` 签名
3. adapter：Behind/Above 合成 + Shadow/Glow `spread=0` + 测试
4. SolidStroke + AlphaEdgeDetect + Shadow/Glow spread + Stroke + 测试
5. Bridge + Inspector + 时间轴

---

## 后续（明确不做）

1. Gradient Overlay
2. Inner Shadow / Inner Glow / Bevel
3. Outer Glow 渐变色 / noise / technique / jitter
4. Precomp / Group 组级 isolation
5. 导出 Layer Style
6. hit-test 随装饰扩 bounds
