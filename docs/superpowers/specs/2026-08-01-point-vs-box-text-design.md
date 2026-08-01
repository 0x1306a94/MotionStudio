# 点文本 / PAG 框文本 — 设计说明

日期：2026-08-01  
状态：待审阅  
相关：取代 `2026-07-30-figma-style-resize-box-text-design.md` 中文本语义（`boxTextMode` = clip/shrink）；PAG 导出见 `2026-07-31-pag-export-design.md`（实现时同步修订）

## 目标

对齐 AE / PAG 的点文本与框文本：

1. **默认点文本**：无排版框；仅 `\n` 换行；不裁剪；不可拖/编 `size`
2. **框文本** = PAG `boxText`（AE 段落文本）：按 `size` 换行；**自动缩字号**使内容完整落在框内（对齐 PAG `AdjustToFitBox`）
3. **去掉文本 `clipRect`**（点/框都不做 canvas 硬裁）
4. 仅框文本可选中拖动改 `content.size`；点文本无缩放角/边手柄
5. `fontSize` / 框 width·height **不支持关键帧**（改为静态属性）；不做旧工程兼容

## 非目标

- Text Animator（逐字动画）——本次不实现；点文本去掉裁剪是为后续留路
- 精确复刻 PAG `AdjustToFitBox` 的每一条启发式（用现有 `textlayout` 二分 shrink 即可）
- 文本层以外图层的 free-transform 行为变更

---

## §1 模型

### `TextContent`（语义变更）

```cpp
class TextContent : public LayerContent {
    Animatable<std::string> text{std::string{"Text"}};  // 保持 Animatable（现有行为不变）
    std::string fontFamily{"PingFang SC"};
    std::string fontStyle{};
    float fontSize = 48.0f;           // 静态；框模式下为「字号上限」，shrink 可小于此值
    Vec2 size{400, 120};              // 静态；点文本排版/选中忽略；切到框文本时可能被测字形覆盖
    bool boxTextMode = false;         // false=点文本（默认）；true=PAG 框文本
    TextAlign align = TextAlign::Left;
};
```

| `boxTextMode` | 含义 |
| --- | --- |
| `false`（默认） | AE / PAG **点文本**：无限宽；仅 `\n`；不 clip；不编辑 `size` |
| `true` | PAG **框文本**：`size` 为排版框；换行 + shrink 完整入框；可拖/编 `size` |

字段名保持 `boxTextMode`，**不再**表示「关=clip / 开=shrink」。

### 序列化

- `fontSize`、`size` 以静态 JSON 数值写出；**不**写关键帧数组
- 加载侧不解析这些属性上的旧关键帧（工程早期无此类文档，不迁移）
- `boxTextMode` 默认 `false`

### 创建文本层

- `boxTextMode = false`
- `fontSize = 48`（或现有默认）
- `size` 可先写占位（如 400×120），点文本排版忽略它；选中 bounds 用字形测量

---

## §2 排版与渲染

### 点文本

- `boxWidth` 视为无限（或足够大），**禁止**按宽度软换行
- 仅硬换行 `\n`
- **不**调用 `clipRect`
- 绘制字号 = `fontSize`（不 shrink）

### 框文本

- 按 `size.x` / `size.y` 换行
- `shrinkToFit = true`：二分缩字号，使全部行落在框内（现有 `LayoutText` 路径）
- **不**调用 `clipRect`（完整显示依赖 shrink，而非裁剪）

### Adapter

`TgfxCanvasAdapter::drawText`：

- 去掉无条件 `clipRect`
- `boxTextMode == false`：走点文本布局输入（不 wrap、不 shrink）
- `boxTextMode == true`：wrap + shrink

### 选中 / Hit bounds

| 模式 | bounds |
| --- | --- |
| 点文本 | 当前帧字形（layout 后）轴对齐包围盒，经 layer transform |
| 框文本 | `content.size` 矩形（与现有固定框一致） |

---

## §3 交互

### 画布手柄（单选文本）

| 模式 | 移动 | 旋转 / 锚点 | 角点 / 边中点 resize |
| --- | --- | --- | --- |
| 点文本 | ✓ | 保持现有 | **不显示**（不 hit） |
| 框文本 | ✓ | 保持现有 | ✓ 只写 `content.size`（+ 现有 position/anchor 补偿）；**禁止**写 `transform.scale` |

多选含点文本：无 `content.size` 的文本不参与「改 size」分支；行为与现有「无容器尺寸对象」一致（只跟 position 或跳过 resize 写入）。

### 属性面板（文本）

| 控件 | 点文本 | 框文本 |
| --- | --- | --- |
| Font Size | 可编辑；**无**关键帧按钮 | 同左（表示字号上限） |
| Width / Height | **置灰**（不可编辑） | 可编辑；**无**关键帧按钮 |
| 框文本模式 Toggle | 可切换 | 可切换 |
| Transform Scale | 仅面板手动；画布不拖 scale（沿用全局「手柄不写 scale」） | 同左 |

### 点文本 → 框文本

在关闭 `boxTextMode` 的 undo 命令/桥接 API 内（与 `setBoxTextMode(true)` 同一事务）执行：

1. 在**当前播放头**求值 `text` 字符串；用当时的静态 `fontSize`、字体、align
2. 按点文本规则布局（无限宽、仅 `\n`、不 shrink）
3. 测量布局结果的内容包围盒宽高（与点文本选中 bounds 同源；行高用 ascent/descent，不是 path tight bounds）
4. `size = max(measured, Vec2{1, 1})`，再设 `boxTextMode = true`

框文本 → 点文本：只关 `boxTextMode`；保留 `size` / `fontSize`，不改测。

---

## §4 PAG 导出

| MS | PAG `TextDocument` |
| --- | --- |
| `boxTextMode == false` | `boxText = false`；`boxTextSize = 0`；`firstBaseLine = 0` |
| `boxTextMode == true` | `boxText = true`；`boxTextSize = size`；`boxTextPos = (0,0)`；`firstBaseLine ≈ fontSize * 0.8` |

- 删除「shrink 无法表达」类 `TextFeatureApproximated` warning（MS shrink 对齐 PAG 框文本适配）
- `fontSize` / `size` 仅静态导出（本就无关键帧）

同步修订 `docs/superpowers/specs/2026-07-31-pag-export-design.md` §3.4。

---

## §5 测试要点

- 点文本：长串无空格不按宽断行；`\n` 断行；绘制无 clip；手柄无 scale 角/边
- 框文本：超框触发 shrink；改 `size` reflow；可拖 resize
- 点→框：`size` 等于测得字形尺寸（允许小数值误差）
- 序列化：`fontSize`/`size` 无 keyframes 节点；round-trip
- PAG：点文本 `boxText=false`；框文本 `boxText=true` + `firstBaseLine` 非 0
- 属性面板：点文本 width/height 置灰；无 KF 按钮

---

## §6 实现触及面（预估）

| 区域 | 变更 |
| --- | --- |
| `TextContent` + Serializer + undo/commands | 静态 `fontSize`/`size`；命令/桥接 API |
| `textlayout` + `TgfxCanvasAdapter::drawText` | 点文本布局；去 clip |
| SceneEvaluator / bounds / SelectionHandles | 点文本内容 bounds；框才挂 size 手柄 |
| FreeTransformDrag / Canvas VC | 点文本不进 contentSize resize |
| TextLayerInspector | 置灰 size；去 KF |
| PagFileBuilder + 测试 + 文档 | 导出映射与 warning |

---

## 已确认决议

1. 方案 1：`boxTextMode` ↔ PAG `boxText`；默认点文本  
2. 框文本：换行 + 自动缩字号完整显示；无 canvas clip  
3. 点文本手柄：方案 A（不显示 resize 手柄）  
4. `fontSize` / width / height：静态、无关键帧；不考虑旧关键帧兼容  
5. 点→框：用**当前字形测量**设置 `size`  
