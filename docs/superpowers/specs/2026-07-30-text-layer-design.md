# Text Layer — 设计说明

日期：2026-07-30  
状态：已落地（Tasks 1–11）  
分支：`feature/0x1306a94_text_layer`

## 目标

端到端可用的文本图层（接近 PAG 框文本子集）：

1. 虚拟容器内排版：宽约束换行；`autoHeight` 或固定高缩字
2. 填充 / 描边复用 `Layer.styles`（按顺序全部参与，各自 blend）
3. 系统已安装字体（`fontFamily` + `fontStyle`，默认 PingFang SC / 空 style）；自定义字体由用户安装到系统
4. 多行（`\n`）+ 水平对齐（左/中/右）
5. 画布绘制、选中、undo、存盘重开；Inspector 编辑文案

## 现状

已落地：模型 / 序列化 / undo / textlayout / DrawText / tgfx 绘制 / Bridge / App Inspector 与画布拖角。详见 plan `docs/superpowers/plans/2026-07-30-text-layer.md`。

## 非目标

- 画布双击原地编辑
- 垂直对齐、字距 / 行距、富文本 span
- CoreText / HarfBuzz（首版用自研框排版 + tgfx 量字）
- 项目内嵌 Font Asset（用户自行安装系统字体）
- Lottie / PAG 文本导出
- 复杂文种双向排版

---

## §1 数据模型

```cpp
enum class TextAlign : uint8_t { Left = 0, Center = 1, Right = 2 };

class TextContent : public LayerContent {
    Animatable<std::string> text{std::string{"Text"}};
    std::string fontFamily{"PingFang SC"}; // 系统字体族名
    std::string fontStyle{};               // 族内 style（如 Bold）；空 = 默认/Regular
    Animatable<float> fontSize{48.0f};    // 字号上限（固定高时由排版缩字）
    Animatable<Vec2> size{Vec2{400, 120}}; // 虚拟容器；宽始终约束换行
    bool autoHeight = true;               // true：高度随内容；false：固定高 + 缩字
    TextAlign align = TextAlign::Left;
};
```

**填充 / 描边**：复用 `Layer.styles`（按顺序全部 Fill/Stroke，各自 blend）。无 style 时绘制默认黑色 Fill；Stroke 宽度 ≤ 0 跳过。文本忽略 Stroke Position / Trim。

**新建层约定**

- 文案 `"Text"`，`size = 400×120`，`autoHeight = true`，`align = Left`
- `fontFamily = "PingFang SC"`，`fontStyle` 空
- `anchorPoint = (200, 60)`，`position` = 合成中心
- 附带一个黑色 Fill（与形状层新建时 styles 习惯一致）

**PropertyPath**

- 已有：`content.text`、`content.fontSize`
- 新增：`content.size`
- 非 Animatable：`autoHeight`、`align`、`fontFamily`/`fontStyle` → `SetTextFontCommand` 等专用 bridge + undo

**字体**：模型存 CT `fontFamily` + `fontStyle`。Inspector 一级列表用 `UIFont.fontNames`（PostScript face），选中时拆成 family+style 写入；展示文案与列表同为 face 名。不设项目 Font Asset；加载时跳过历史 `type:"font"` asset 与忽略 `fontAssetId` 字段，再保存即消失。

**序列化**：保存写 `fontFamily`/`fontStyle`；加载时 `fontStyle` 可缺省（空 = 默认/Regular）。另有 `size`、`autoHeight`、`align`（及已有字段）。开发阶段**不升** `schemaVersion`；不把旧 PostScript face 名拆成 family+style。

**拖改容器尺寸时的锚点（必做）**

选中文本层，拖角或 Inspector 修改 `content.size` 时：

1. `anchorPoint` 按比例同步：  
   `anchor' = (anchor.x * w1/w0, anchor.y * h1/h0)`（若 `w0` 或 `h0` 为 0，该轴保持原值）
2. `transform.position` **不变**  
   → 锚点相对容器比例不变，图层视觉位置不跳
3. `size` 与 `anchor` 写入同一 undo 合并窗口

仅用户改模型 `size` 时触发。`autoHeight` 下排版测得的内容高度只影响 hit/选中测量，**不**回写 `size`/`anchor`。

---

## §2 渲染管线 + TextLayout

### 架构原则

- **Core** 只处理原始数据求值（字符串、字号上限、容器、对齐、`fontFamily`/`fontStyle`、styles）
- **文本排版**独立模块（adapter 侧），供绘制与 hit 共用；不链进 Core
- 管线对齐 Image：自包含 `DrawText` 命令

### 求值（Core）

```cpp
struct TextDrawStyle {
    Color color;
    BlendMode blendMode;
    bool isStroke;
    float strokeWidth;
};

struct EvaluatedTextItem {
    std::string text;
    float fontSize;                 // 模型字号上限（缩字前）
    Vec2 containerSize;             // evaluate(size)
    bool autoHeight;
    TextAlign align;
    std::string fontFamily;
    std::string fontStyle;
    std::vector<TextDrawStyle> styles;  // Layer::styles 顺序；空 → 黑 Fill
    Vec2 hitSize;                   // Core 初值 = containerSize；autoHeight 时可写回测高
};

struct EvaluatedLayer {
    // ...existing...
    std::optional<EvaluatedImageItem> imageItem;
    std::optional<EvaluatedTextItem> textItem;  // 与 shapeItems / imageItem 互斥
};
```

### DrawCommand

新增 `DrawCommandType::DrawText`，字段与 `EvaluatedTextItem` 对齐（自包含，同 `DrawImage`；含 `textStyles`）。

CommandBuilder：对 `textItem` 追加 `DrawText`；track matte 源层若含文本亦需回放（与 Image 一致）。

### TextLayout 模块（推荐 L1）

位置：`adapter/textlayout/`（独立于 Core；tgfx metrics 实现可放其旁或 `adapter/tgfx/`）。

```text
GlyphMetrics          // 抽象：advance(unichar, size) / fontMetrics(size)
TextLayoutInput       // text, boxWidth, optional boxHeight, fontSize, align, metrics
TextLayoutResult      // appliedFontSize, measuredSize, lines[{origin, width, runs}]
TgfxGlyphMetrics      // tgfx::Font 实现
```

| `autoHeight` | 行为 |
| --- | --- |
| `true` | 宽 = `size.x` 换行；高 = 内容高；**不缩字**；hit/选中高 = `measuredHeight` |
| `false` | 宽高固定；二分缩字直至塞进框；hit/选中 = `containerSize`；仍溢出则 **clip** 兜底 |

- 硬换行：`\n`
- 软换行：优先空白，否则按字符（兼顾中文）
- 水平对齐：每行在 `boxWidth` 内 Left / Center / Right
- 垂直：顶对齐（本里程碑不做垂直居中）
- 行高：由 `FontMetrics`（ascent/descent/leading）得出

**字体解析顺序（Adapter）**

1. `Typeface::MakeFromName(fontFamily, fontStyle)`（style 空则用默认 traits）
2. 再否则 `PingFang SC` → `Helvetica`

**调用**：`TgfxCanvasAdapter` 绘制前 `Layout` 一次，再按 `styles` 顺序画 fill/stroke；hit/选中用同一 `Layout` 得到测量框（`autoHeight` 时高度用 `measuredSize.y`）。测量结果不持久化进 Document。

局部坐标：容器矩形为 `[0, 0]–[width, height]`（与 Image 容器一致）；锚点相对该矩形；文本在框内顶起排版。

---

## §3 App / Bridge / 测试

### Bridge

- `ms_document_add_text_layer(compositionId)` → §1 默认值，返回 layerId
- Animatable：`content.text` / `content.fontSize` / `content.size`（含 string 的 get/set，若尚缺则补齐）
- 专用 API：`ms_command_set_text_font(family, style)`、`autoHeight`、`align`
- 拖角改 size：bridge/App 同时提交等比 `anchor` 更新（同一 merge group）

### App

- 工具栏 **Add Text**
- `TextLayerInspector`：文案、字号、容器 W/H、`autoHeight`、对齐、系统 face 一级列表（选中写入 family+style，展示与列表同为 face 名）、复用 Fill/Stroke 面板（文本 Stroke 隐藏 Position/Trim）
- 拖角改 `content.size`，并按 §1 同步等比锚点、保持 position；选中显示锚点、隐藏旋转
- `autoHeight == true` 时仍允许改 `size.y`（存盘用）；仅 `autoHeight == false` 时 `size.y` 参与缩字
- Project 面板仅 Image Asset；**不做**字体导入
- **不做**画布原地编辑

### 测试

| 层 | 覆盖 |
| --- | --- |
| Core | 序列化 round-trip；PropertyPath；Evaluator `textItem`；`DrawText` 命令 |
| TextLayout | 换行、对齐、`autoHeight` 量高、固定高缩字（假 `GlyphMetrics`） |
| Adapter | 快照：单行 / 多行 / 缩字 / fill+stroke（若已有基建） |
| 交互 | 拖 size → 锚点等比、position 不变；undo；改系统字体重开 |

### 验收

1. Add Text → 画布显示苹方「Text」
2. 文案含 `\n` + 改宽 → 换行正确；关 `autoHeight` → 缩字塞进框
3. 多 Fill/Stroke + blend 可见；undo 正确
4. 拖容器角：视觉位置不跳，锚点相对比例保持
5. Inspector 从系统 face 列表切换字体后存盘重开仍正确（模型为 family+style）

---

## 关键接口伪代码（实现指引）

```cpp
// Core — 求值产出原始文本项（无排版）
EvaluatedTextItem EvaluateText(const TextContent&, const Layer& styles, const Document&);

// adapter/textlayout — 与渲染解耦
TextLayoutResult LayoutText(const TextLayoutInput&);

// Adapter — 解析字体 → Layout → clip(可选) → drawTextBlob / stroke
void PlayDrawText(const DrawCommand&, Canvas&);
```
