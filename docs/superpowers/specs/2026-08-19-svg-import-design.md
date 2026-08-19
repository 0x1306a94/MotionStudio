# SVG 导入 → Core 图层树 — 设计说明

日期：2026-08-19  
状态：已确认  
关联：[数据模型](../../data-model.md)、[Vector Network](./2026-08-05-vector-network-pen-design.md)、[Gradient Paint](./2026-08-11-gradient-paint-design.md)、[Figma 对照](../../figma-to-motionstudio.md)

## 目标

把 SVG 文件导入成 MotionStudio 自身的图层树（对标 Figma「导入 SVG → 可编辑图层」），而不是栅格图或不可编辑的渲染快照。

1. 用 tgfx `SVGDOM` 解析 SVG（内部已走 `XMLDOM`）
2. 所有形状节点统一变成 `LayerType::Shape` + `ShapePath` + `Animatable<VectorNetwork>`，便于钢笔二次编辑
3. 单独静态库实现导入，链接 **tgfx + core**；**不**把 tgfx 类型漏进 Core
4. 保留 SVG 节点树（`LayerType::Group` + `parentId`）以及 transform / 可映射样式
5. `<text>` 导入为可编辑 `LayerType::Text`（点文本）；`<textPath>` / 逐字定位本阶段跳过 + diagnostic

## 已锁定决策

| 项 | 选择 |
|---|---|
| 解析入口 | `tgfx::SVGDOM::Make`（内部 `DOM::Make` → `SVGNodeConstructor`） |
| 不采用 | 自写 XML 语义（PAGX `SVGImporter` 那条路）；不调用 `SVGNode::asPath` / `SVGRenderContext`（私有实现） |
| 库位置 | `src/import/svg/` + `include/MotionStudio/import/svg/`，静态库 `svg_import`（alias `motionstudio_svg_import`） |
| 依赖 | PUBLIC `core`；PRIVATE 预编译 `tgfx`（须 `-DTGFX_BUILD_SVG=ON`） |
| 形状几何 | 一律 `ShapePath.path`（`VectorNetwork`）；**不用** `ShapeRect` / `ShapeEllipse` |
| 树结构 | 扁平 `Composition.layers` + `parentId`；`<g>` / 嵌套 `<svg>` → `LayerType::Group` |
| Transform | SVG `Matrix` 分解为 `position` / `rotation` / `scale`；**`anchorPoint` = 局部 AABB 中心**，并补偿 `position` 使世界矩阵不变；含剪切则把残差 bake 进几何 |
| 样式 | 映射到现有 `FillStyle` / `StrokeStyle` / `GradientPaint`；Core 没有的字段跳过并记 warning |
| 文本 | `<text>`（含同样式 `<tspan>`）→ 点文本 `TextContent`；`<textPath>` / 逐字 x,y / `rotate` 跳过 + diagnostic |
| 滤镜 / mask / pattern / dash | 本阶段跳过 + diagnostic |
| `<image>` | data URI → `Image` 层 + `Asset`；外部 href 跳过 + warning |
| `<use>` | 经 `nodeIDMapper` 展开，深度上限 32，环检测 |
| 产出 | **图层树**（根 `Group` + 扁平 `layers` + `parentId`），插入**当前合成**；不新建 `Document` / `Composition`，不改合成尺寸 |
| Undo | `ImportSvgInto` 经 `UndoManager` 执行一条 `CompositeCommand`（`ImportImageAsset` + `AddLayer`）；一次 undo 整棵撤销 |
| App / Bridge | 库层不做 UI。App 接入见 [SVG Import UI](./2026-08-19-svg-import-ui-design.md) |

## 非目标

- 导入后立刻可在 App 里点选导入（无 Bridge / 文件选择器）
- `<textPath>` 沿路径排版、逐字 `x`/`y`/`rotate`、多样式混排进**同一** Text 层（拆成多 Text 层）
- 嵌入字体（`@font-face` / 外部 font 文件）
- SVG 动画（SMIL / CSS animation）
- `stroke-dasharray`（Core `StrokeStyle` 无 dash）
- `pattern`、滤镜图元（`filter` / `fe*`）、`mask`、`marker`
- 为 SVG 增加 Core 字段（skew、dash）；不为 SVG 发明内外描边属性
- 复用 `third_party/libpag/src/pagx/svg/SVGImporter.cpp`（那是 PAGX 节点树，模型不同）
- 升 `schemaVersion`

---

## 1. 现状与为何选 SVGDOM

### 1.1 tgfx 两层 DOM

| API | 角色 |
|---|---|
| `tgfx::DOM` / `DOMNode`（`xml/XMLDOM.h`） | 通用 XML 树：`name` / `attributes` / `firstChild` / `nextSibling` |
| `tgfx::SVGDOM`（`svg/SVGDOM.h`） | 语义 SVG 树：`SVGTag`、presentation 属性、`transform` 已是 `Matrix`、`<path>` 已是 `tgfx::Path` |

`SVGDOM::Make` 流程已经固定：

```
Stream → DOM::Make → SVGNodeConstructor::ConstructSVGNode
      → SetClassStyleAttributes(cssMapper) → SVGDOM{root, nodeIDMapper}
```

构造器支持的元素包括：`svg` / `g` / `path` / `rect` / `circle` / `ellipse` / `line` / `polygon` / `polyline` / `use` / `image` / `defs` / `linearGradient` / `radialGradient` / `clipPath` / `mask` / `filter` / `pattern` / `text`…。`<style>` 会进 `CSSMapper`，class 规则写回节点属性。

PAGX `SVGImporter` 也是 `XMLDOM` + 自写 `convertToLayer`。MotionStudio **不走那条路**：重复实现单位、继承、path 语法，且产出是 PAGX 而不是 `motion::Layer`。

### 1.2 不用 `asPath` / `SVGRenderContext`

`SVGNode::asPath` / `asPaint` 依赖 `src/svg/SVGRenderContext.h`（不在 public include）。导入库只使用：

- 公开头：`SVGDOM`、`SVGNode` 及子类 getter、`SVGLengthContext`、`SVGPathParser`、`tgfx::Path`
- 几何：读 typed 属性（`getShapePath()` / `Cx` / `Points`…），用公开 `SVGLengthContext` 把长度收成 px，再自己 `Path.addRect` / `addOval` / 迭代 `Path::Iterator`

这样不链 tgfx 私有 `src/`，也不绑渲染上下文。

### 1.3 预编译 tgfx 必须开 SVG

`third_party/libpag/third_party/tgfx/CMakeLists.txt`：`option(TGFX_BUILD_SVG ... OFF)`，仅 `if (TGFX_BUILD_SVG)` 才编 `src/svg/*`。

当前根 `CMakeLists.txt` 的 `TGFX_CMAKE_ARGS` **没有** `-DTGFX_BUILD_SVG=ON`。导入库落地时必须加上，并接受 `build/tgfx_prebuilt/` 全量重编。

### 1.4 Core 已有、导入直接用

| Core | 用途 |
|---|---|
| `LayerType::Shape` + `ShapeContent.geometry` | 单几何；Fill/Stroke 在 `Layer::styles` |
| `ShapePath.path` : `Animatable<VectorNetwork>` | 权威可编辑路径 |
| `BezierPathToVectorNetwork` / `Path::Iterator` | 轮廓 → Network |
| `LayerType::Group` + `NullContent` + `parentId` | SVG 组（见 `ShapeElement.h` 注释） |
| `Transform`：anchor / position / scale / rotation / opacity | 无 skew |
| `FillStyle` / `StrokeStyle` | Color / Gradient / Shader；`StrokePosition` 已有 Center / Inside / Outside |
| `GradientPaint` | Linear / Radial / Conic / Diamond；坐标是层局部 AABB 左上角空间 |
| `Mask` : `Animatable<VectorNetwork>` | 可选：简单 `clip-path` |
| `LayerType::Image` + `Asset` | data URI 图 |
| `AddLayerCommand` / `ImportImageAssetCommand` / `CompositeCommand` | 插入当前合成的 undo 单元 |
| `UndoManager`（挂在 bridge `MSDocument`，不在 `Document` 上） | `ImportSvgInto` 的入参 |

Core **没有**：`stroke-dasharray`、矩阵 skew。`StrokeStyle.position` 已有 Center / Inside / Outside；SVG 描边没有内外对齐，导入只能写成 `Center`。

---

## 2. 库边界

对标 `src/export/pag`（转换库，PUBLIC core，PRIVATE 预编译 tgfx），而不是把逻辑塞进 `core`。

```
include/MotionStudio/import/svg/SvgImporter.h   # 唯一对外头，禁止 include tgfx
src/import/svg/
  CMakeLists.txt
  SvgImporter.cpp          # BuildSvgLayers / ImportSvgInto
  SvgWalk.cpp / .h         # 遍历 SVGNode
  SvgStyle.cpp / .h        # 继承 + 映射 Fill/Stroke/Gradient
  SvgTransform.cpp / .h    # Matrix → Transform，残差 bake
  SvgPathConvert.cpp / .h  # tgfx::Path → VectorNetwork
  SvgLength.cpp / .h       # viewport + SVGLengthContext 包装
tests/import/svg/
  SvgImporterTest.cpp
  fixtures/*.svg
```

根 `CMakeLists.txt`（`if(APPLE)`，`adapter/tgfx` 之后）：

```cmake
add_subdirectory(src/import/svg)
```

`svg_import`：

- `OUTPUT_NAME motionstudio_svg_import`
- PUBLIC：`${PROJECT_SOURCE_DIR}/include`、`core`
- PRIVATE：`${TGFX_INCLUDE_DIR}`，`motionstudio_add_tgfx_prebuild` + `motionstudio_link_prebuilt_tgfx`
- 测试二进制 `svg_import_tests`（gtest_discover_tests），与 `pag_export_tests` 同级

C++ 命名空间：`motion::svg`。

**Core 禁令**：`src/` 里现有 core 目标不得 `#include` tgfx，也不得反向依赖 `svg_import`。

---

## 3. 对外接口

导入产出是**可挂到现有合成上的图层树**，不是新文档。`UndoManager` 由宿主持有（bridge `MSDocument`），库不拥有它。

```cpp
namespace motion::svg {

struct ImportOptions {
    int insertIndex = -1;          // 根 Group 插入位置；-1 = 追加到顶
    EntityId parentLayerId{};      // 无效 = 合成根下；有效则根 Group 再挂到该层下
    std::string rootName{"SVG"};   // 根 Group 名（宿主可传入文件名）
};

struct Diagnostic {
    enum class Severity { Warning, Error };
    Severity severity = Severity::Warning;
    std::string code;
    std::string message;
    std::string nodeName;
};

struct EmbeddedImage {
    EntityId assetId;
    std::string suggestedFileName;
    std::vector<uint8_t> bytes;
};

struct SvgLayerTree {
    std::vector<std::unique_ptr<Layer>> layers;  // 底→顶；parentId 已在树内接好
    std::vector<Asset> assets;
    std::vector<EmbeddedImage> embeddedImages;
    std::vector<Diagnostic> diagnostics;
    int sourceWidth = 0;   // SVG 固有尺寸（信息量；不改合成）
    int sourceHeight = 0;
};

struct ImportResult {
    EntityId rootLayerId;              // 包裹用 Group
    std::vector<EntityId> layerIds;    // 全部插入层，底→顶
    std::vector<Diagnostic> diagnostics;
    int sourceWidth = 0;
    int sourceHeight = 0;
};

// 纯转换：不碰 Document / UndoManager。测试与 ImportSvgInto 共用。
Expected<SvgLayerTree, std::string> BuildSvgLayers(const void *bytes, size_t length,
                                                   const ImportOptions &options = {});
Expected<SvgLayerTree, std::string> BuildSvgLayersFromFile(const std::string &path,
                                                           const ImportOptions &options = {});

// 插入当前合成，一条 CompositeCommand，可一次 undo。
// composition 不存在 → Unexpected。部分节点跳过仍 hasValue()。
Expected<ImportResult, std::string> ImportSvgInto(Document &document, UndoManager &undo,
                                                  EntityId compositionId, const void *bytes,
                                                  size_t length,
                                                  const ImportOptions &options = {});
Expected<ImportResult, std::string> ImportSvgFileInto(Document &document, UndoManager &undo,
                                                      EntityId compositionId,
                                                      const std::string &path,
                                                      const ImportOptions &options = {});

}  // namespace motion::svg
```

### 3.1 `ImportSvgInto` 行为

1. `BuildSvgLayers` 得到树（失败则不改文档）
2. 目标 `Composition` 必须存在；**不改**其 width / height / frameRate / duration / background
3. 每层 `inPoint = 0`，`outPoint = composition.duration`
4. 永远先做一个根 `Group`（`options.rootName`），viewBox 映射矩阵在这层；可见内容都是它的子孙
5. 组一条 `CompositeCommand("Import SVG")`：
   - 每个 `Asset` → `ImportImageAssetCommand`（先于图层，以便 `assetId` 能解析）
   - 每个 `Layer` → `AddLayerCommand(compositionId, layer, index)`  
     根 Group 用 `insertIndex`；其后各层追加（或插在根之后保持文档序）。层上已写好 `parentId`，`AddLayerCommand` 原样带进文档
6. `undo.execute(document, composite)` — 一次 undo 去掉全部新层和新 asset（磁盘文件本就不由 `ImportImageAssetCommand` 删除）
7. `embeddedImages` 仍返回给宿主写 `projectRoot/assets/`；**写盘不是 undo 的一部分**（与现有 Import Image 一致）

`parentLayerId` 有效时：根 Group 的 `parentId = parentLayerId`，插入前用 `setParent` 防环；会成环则 `Unexpected`，不执行命令。

公开头只出现 `motion::` 类型。

---

## 4. 节点 → 图层

### 4.1 总表

| SVG | Core | 备注 |
|---|---|---|
| 根 `<svg>` | 根 `Group`（viewBox 映射挂在这层） | 不新建合成、不改画布尺寸 |
| `<g>`、嵌套 `<svg>` | `LayerType::Group` | 子节点 `parentId = group.id` |
| `path` / `rect` / `circle` / `ellipse` / `line` / `polygon` / `polyline` | `Shape` + `ShapePath` + `VectorNetwork` | 见 §6 |
| `<use>` | 展开引用子树（拷贝后当普通节点） | 环或超深 → skip |
| `<image>` data URI | `Image` + 待插入的 `Asset` | `size` = 解析后的宽高 |
| `<image>` 外部 href | 跳过 | `image.external` |
| `<defs>` / `linearGradient` / `radialGradient` / `clipPath` / `mask` / `filter` / `pattern` / `marker` / `stop` / `style` / `title` / `desc` / `metadata` | 不进图层列表 | defs / 渐变 / clip 作查找表 |
| `<text>` + 同样式 `<tspan>` / 字面量 | `Text` + `TextContent`（点文本） | 见 §8.3 |
| `<textPath>` | 跳过 | tgfx 未做沿路径排版；`textPath.skipped` |
| 异样式 / 自带 x,y 的 `<tspan>` | 额外 Text 层（兄弟） | 见 §8.3 |
| `display="none"` | 整棵子树跳过 | 无图层 |
| `visibility="hidden"` / `collapse` | 仍建层，`visible = false` | 保留树 |

未知标签：跳过 + `tag.unknown`。

### 4.2 树与 z 序

Core 图层是**扁平数组**，父子只靠 `parentId`（`data-model.md`：组变换不进形状树）。

- `layers[0]` = 最底，向上画
- SVG 文档序 = 画家算法（先画的在下）→ 子节点按文档序 `addLayer` 追加
- 组本身也占一层，排在其第一个子层之前，便于侧栏成组
- `BuildSvgLayers` **不**调用 `Document::addLayer`；只在 `unique_ptr<Layer>` 上写 `parentId`
- `ImportSvgInto` 再按序 `AddLayerCommand`；`parentId` 指向的层此时已在同一棵导入树里（或 `options.parentLayerId`）

伪代码：

```
walk(node, parentLayer):
  if skipTag(node): return
  if textPath: warn("textPath.skipped"); return
  layer = makeLayer(node)          # Group / Shape / Image / Text
  if parentLayer: layer.parentId = parentLayer.id
  layers.push_back(layer)
  if isGroup(node):
    for child in node.children:
      walk(child, layer)
assignCenterAnchors(layers)        # 后序，见 §5.2
```

### 4.3 命名

优先级：`id` → 非空 `class` 第一段 → 标签默认名（`Path` / `Rectangle` / `Ellipse` / `Line` / `Polygon` / `Group` / `Text`）。同名允许，不做自动编号。

---

## 5. Transform 与 viewBox

### 5.1 视口与合成尺寸

`SVGRoot`：`Width` / `Height`（默认 100%）+ 可选 `ViewBox`。

与 `SVGDOM::getContainerSize()` 对齐：

1. 有 viewBox：用 viewBox 尺寸当 length viewport，resolve `width`/`height` → `targetW/H`
2. 无 viewBox：viewport 先当 100×100，resolve `width`/`height`
3. resolve 后任一边 ≤ 0：失败（`Unexpected`）
4. `sourceWidth/sourceHeight = max(1, round(target))`，只写入 `SvgLayerTree` / `ImportResult`，**不写**当前合成

viewBox 原点非 0，或 viewBox 尺寸 ≠ 目标尺寸时，映射矩阵加在**根 Group**上。算法与 `SVGNode::ComputeViewboxMatrix` 同语义（protected，导入库**重实现**）。`preserveAspectRatio` 读根属性；缺省 `xMidYMid meet`。内容仍落在 SVG 用户空间（映射后），宿主若要居中当前画布，自己移根 Group。

### 5.2 节点矩阵 → `Transform`（中心锚点）

`SVGTransformableNode::getTransform()` 是 `tgfx::Matrix`。Core 局部矩阵：

```
T(position) * R(rotation) * S(scale) * T(-anchor)
```

先按 SVG 原点分解线性部分：

```
a c tx
b d ty
scaleX = hypot(a, b)
scaleY = hypot(c, d) * sign(det)
rotationDeg = atan2(b, a) * 180/π     # 与 Mat3::Rotate 同一套 2D 矩阵
translation = (tx, ty)
```

`det ≈ 0`：该节点 bake 整矩阵进几何（组则 bake 到后代几何），层 transform 保持单位后再走中心锚点。

存在剪切时：

1. 仍取上面的 R/S（忽略剪切）
2. 残差 `M * inverse(T(translation)*R*S)` bake 进该节点几何（组：乘到后代路径点 / 图位置）
3. **Text 不 bake 字形**：只保留 TRS，记 `text.shear`

然后**后序**把锚点放到局部 AABB 中心，并补偿 position，使世界矩阵与「锚在原点」时相同：

```
anchor = localAabb.center
position = translation + Rotate(rotation) * Scale(scale) * anchor
```

推导：`T(translation)*R*S = T(position)*R*S*T(-anchor)` 当 `position = translation + R*S*anchor`。

局部 AABB：

| 层 | `localAabb` |
|---|---|
| Shape | `VectorNetwork` 顶点轴对齐包围盒（不含描边宽度） |
| Image | `[0, size.x] × [0, size.y]`（图像内容从层局部原点起，与现有 Image 层一致） |
| Text | 点文本字形盒：原点与 Core 排版一致（首行顶 ≈ 0，基线 ≈ ascent）；宽高用 tgfx `TextBlob` 度量 |
| Group | 每个直接子层 AABB 四角经该子层 `T(pos)*R*S*T(-anchor)` 变到本组局部后取并 |
| 空盒 / 无子 | 退回 `anchor = (0,0)`，`position = translation` |

不把几何点平移到原点：路径仍是 SVG 局部坐标，只改 `anchor`/`position`。旋转/缩放手柄落在视觉中心，和现有「Rect 几何居中时 anchor=(0,0) 即中心」同一交互，只是 SVG 坐标原样保留。

渐变的 AABB 左上角空间仍用几何 `bounds.min`，与锚点无关。

`opacity`（非继承）→ 该节点对应层的 `transform.opacity`。Core 求值会沿 `parentId` **连乘**祖先 opacity（`WorldOpacityOf`），因此组上的 opacity 会传到子层。

已知差异：SVG 组 opacity 是「先画组再乘透明」（isolation）；Core 是逐层相乘。子层重叠处可能比 SVG 更不透明。本阶段接受该差异，不为 Group 做离屏 isolation。

### 5.3 几何空间

形状点落在**该层局部**（未乘本层 transform）。SVG 几何属性在节点局部用户空间，resolve 后直接进 `VectorNetwork`，不再乘本层 `getTransform()`。

---

## 6. 形状 → VectorNetwork

### 6.1 先变成 `tgfx::Path`

在当前 viewport 的 `SVGLengthContext` 下：

| 标签 | 构造 |
|---|---|
| `path` | `SVGPath::getShapePath()`（已解析） |
| `rect` | resolve x/y/w/h 与可选 rx/ry → `addRect` / `addRoundRect` |
| `circle` | cx/cy/r → `addOval` |
| `ellipse` | cx/cy/rx/ry → `addOval` |
| `line` | x1/y1 → x2/y2，`moveTo`/`lineTo`（开口，通常只有 stroke） |
| `polygon` / `polyline` | `getPoints()` 折线；polygon `close()` |

`rx/ry` 按 SVG 夹到半宽/半高。零面积且无 stroke 的形状：跳过 + `shape.empty`。

**不**为了「保持参数矩形」而生成 `ShapeRect` / `ShapeEllipse`。圆角 rect / 圆在 Network 里是若干段三次贝塞尔，和 Figma 导入后的可编辑矢量一致。

### 6.2 `Path` → `VectorNetwork`

`tgfx::Path::Iterator` 的 verb：Move / Line / Quad / Conic / Cubic / Close。

| verb | 边 |
|---|---|
| Move | 新轮廓起点顶点 |
| Line | 边，切线 = 0 |
| Cubic | `startTangent = c1 - p0`，`endTangent = c2 - p3` |
| Quad | 升阶：`C1 = P0 + 2/3 (Q-P0)`，`C2 = P2 + 2/3 (Q-P2)` |
| Conic | 用标准 conic→cubic 近似（一段 conic 拆成 1 或 2 段 cubic），再按 Cubic 写入；记 `path.conic` |
| Close | 末点连回轮廓起点；末==起点则只闭合成环，不复制顶点 |

约定：

- 顶点/边 id 从 1 递增（与 `BezierPathToVectorNetwork` 一致）
- 同一轮廓内，Close 与相邻段**共享**端点 id
- 多轮廓 → 一个 Network 里多个连通片（合法）
- 不同轮廓的重合点 **不** 合并（避免误共享）
- `mirrorMode = None`
- 不允许自环；退化零长度边丢弃

填充时渲染走已有 `CompileFillFaces`；描边走 `CompileStrokeEdges`。导入不预编译，权威数据只有 Network。

---

## 7. 样式映射

### 7.1 继承

presentation 属性在节点上可能是 Unspecified / Inherit / Value（`SVGProperty`）。导入时自祖先向叶子收一份 **ComputedStyle**：

可继承：`fill`、`fill-opacity`、`fill-rule`、`stroke`、`stroke-opacity`、`stroke-width`、`stroke-linecap`、`stroke-linejoin`、`stroke-miterlimit`、`stroke-dasharray`、`color`（`currentColor`）、`font-family`、`font-size`、`font-style`、`font-weight`、`text-anchor`。

不可继承、只看本节点：`opacity`、`display`、`visibility`、`clip-path`、`mask`、`filter`。

SVG 初始值（节点链上都未指定时）：

- `fill` = 黑色
- `stroke` = none
- `fill-rule` = nonzero
- `stroke-width` = 1
- `stroke-linecap` = butt
- `stroke-linejoin` = miter
- `stroke-miterlimit` = 4
- 各 opacity = 1

`fill="none"`：不加 `FillStyle`。`stroke="none"` 或不画线：不加 `StrokeStyle`。两者都无且不是 `<image>` / `<text>`：仍建 Shape（空 styles），便于以后上色。

### 7.2 Fill

| SVG | Core |
|---|---|
| `paint-color` | `FillStyle.paintMode = Color`，`color` 静态值 |
| `fill-opacity` × 颜色 alpha | 写入 `Color.a` |
| `fill-rule` evenodd / nonzero | `FillRule::EvenOdd` / `NonZero` |
| `currentColor` | 用继承的 `color` |
| `url(#id)` 线性/径向渐变 | `paintMode = Gradient`，见 §7.4 |
| `url(#id)` pattern / 缺失 | 跳过该 fill + `paint.unresolved` |
| style 级 blend | Core 有 `FillStyle.blendMode`；tgfx 不解析 `mix-blend-mode` → 保持 `Normal` |

组上的 fill **不**复制到 Group 层（Group 的 `styles` 不参与绘制）。只写在叶子 Shape / Text 上。

### 7.3 Stroke

| SVG | Core |
|---|---|
| 颜色 / 渐变 | 同 Fill，挂在 `StrokeStyle` |
| `stroke-width` | `width`（px，`LengthType::Other`） |
| `stroke-linecap` | `LineCap` Butt/Round/Square |
| `stroke-linejoin` | `LineJoin` Miter/Round/Bevel；`Inherit` 当 Miter |
| `stroke-miterlimit` | `miterLimit` |
| 对齐 | 恒 `StrokePosition::Center`（SVG 无 Inside/Outside；Core 字段本身支持） |
| `stroke-dasharray` / `dashoffset` | 忽略 + `stroke.dash` |
| trim | 保持默认 0–1（SVG 无 trim） |

只有 stroke 的线（`line` / 开口 path）：只加 `StrokeStyle`。

### 7.4 渐变

`SVGPaint::Type::IRI` → `nodeIDMapper` → `SVGLinearGradient` / `SVGRadialGradient`。`href` 链最多 8 步，收集 `SVGStop`（`offset`、`stop-color`、`stop-opacity`）。

stops < 2：该 paint 跳过 + `gradient.stops`。

坐标：

- `gradientUnits = objectBoundingBox`：x1.. 是 0–1 比例，乘形状 AABB 后换成 **AABB 左上角空间**（`GradientPaint`：`(0,0) = bounds.min`）
- `userSpaceOnUse`：px 先减 `bounds.min` 再写入 `start`/`end`

映射：

| SVG | `GradientPaint` |
|---|---|
| linear `x1,y1 → x2,y2` | `type = Linear`，`start`/`end` |
| radial `cx,cy,r` | `type = Radial`，`start = 圆心`，`end = start + (r, 0)` |
| `fx/fy` ≠ 圆心 | 忽略焦点 + `gradient.focal`（Core 径向无焦点） |
| `spreadMethod` repeat/reflect | 忽略 + `gradient.spread`（Core 无 tile） |
| `gradientTransform` | 乘到 start/end（线性两端；径向圆心与半径点） |

`offset` 夹到 [0,1]，按 offset 排序。

### 7.5 clip-path（有限）

`clip-path=url(#id)` 且 clip 内容是**单个**可转 Network 的形状、无额外滤镜：写入 `Layer.masks[0]`，`MaskMode::Add`，`opacity=1`，`inverted=false`。

`clip-rule` evenodd：仍进同一个 Network，靠填充规则；Core Mask 无独立 fill-rule 字段时只转几何。

多子节点 / `clipPath` 套 `use` 过深：跳过 + `clip.unsupported`。`mask` 属性一律跳过 + `mask.skipped`。

### 7.6 明确不映射

| SVG | 处理 |
|---|---|
| `filter` / `feGaussianBlur` 等 | 跳过（不写 `LayerEffect`）`filter.skipped` |
| `marker-*` | 跳过 |
| `vector-effect="non-scaling-stroke"` | 跳过，描边按局部宽度 |
| CSS 复杂选择器（非 class） | tgfx 本来就不收；无额外工作 |

每个跳过码只对**同一节点**报一次。

---

## 8. `<use>` 与 `<image>`

### 8.1 use

`SVGUse`：`Href` 本地 IRI + 可选 `x`/`y`。

1. `nodeIDMapper` 找目标；找不到 → `use.missing`
2. 深度 > 32 或 id 栈成环 → `use.cycle`
3. 目标当子树 walk，外面包 Group，transform = `translate(x,y) * use.transform`
4. 展开结果是独立图层，改副本不影响 defs 源

### 8.2 image

- `SVGIRI::DataURI`：解码像素（tgfx `SVGImage::LoadImage` 或 codec），`Asset{type=Image, name, width, height}`，`path` 记逻辑名 `"assets/<id>.png"`（**库不写盘**）。`ImageContent.assetId`、`size` = 元素 resolve 后的 w/h，`scaleMode = LetterBox`（有 `preserveAspectRatio` 时：`none` → Stretch，否则 LetterBox）。
- 非 data URI：不建层，`image.external`。
- 字节在 `SvgLayerTree.embeddedImages`；`ImportSvgInto` 执行后宿主仍可用同一份写盘。`<image>` 的 `x/y` 并进该节点矩阵（`T(x,y) * svgTransform`）再按 §5.2 分解；`ImageContent` 无独立平移字段。

### 8.3 Text

导入为**可编辑点文本**，不转字形轮廓。

| SVG | Core |
|---|---|
| `<text>` | `LayerType::Text` + `TextContent`，`boxTextMode = false` |
| 字面量 + 同样式、无独立 x/y 的 `<tspan>` | 拼进同一 `content.text`（保留 `\n`） |
| 自带 x/y 或不同 fill/stroke/font 的 `<tspan>` | **另建**兄弟 Text 层（Core 一层一种样式） |
| `<textPath>` | 不建层，`textPath.skipped`（tgfx `SVGTextPath::onShapeText` 未沿路径排） |
| `x`/`y`/`dx`/`dy` 数组长度 > 1 或 `rotate` | 只用第一项；记 `text.glyphPositions` / `text.rotate` |
| `text-anchor` | `start`→`Left`，`middle`→`Center`，`end`→`Right` |
| `font-size` | resolve 后的 px → `fontSize`；未指定 = **16**（SVG 初始值） |
| `font-family` | 取列表第一项（去引号）；空 / inherit → `"PingFang SC"` + `font.fallback` |
| `font-weight` + `font-style` | `Bold` / `Italic` / `Bold Italic` / `""`（Regular） |
| fill / stroke | 与 Shape 同一套 ComputedStyle，写入 `Layer.styles`（文本 Stroke 的 Position/Trim 无效，仍写 `Center`） |

**矩阵：** SVG `(x,y)` 是**基线对齐点**（`text-anchor` 决定左右）。Core 点文本局部原点在首行顶附近（`line.y = metrics.ascent`，`line.x` 按 align 落在行盒内）。分解前补偿：

```
alignX = 0 | measuredWidth/2 | measuredWidth     # Left / Center / Right
M = getTransform() * T(x0+dx0 - alignX, y0+dy0 - ascent)
```

`ascent` / 行宽用 tgfx `Font` + `TextBlob`（库已 PRIVATE 链 tgfx）。找不到 typeface 时：`ascent = fontSize * 0.8`，`width = fontSize * utf8CodePointCount * 0.5`，仍建层。

然后按 §5.2 中心锚点。Text 局部 AABB = 该度量盒（原点已是 Core 排版空间）。

空字面量（去空白后空）：不建层，无 diagnostic。

---

## 9. 错误与 diagnostic 码

| 码 | 含义 |
|---|---|
| `textPath.skipped` | `<textPath>` 未导入 |
| `text.glyphPositions` | 逐字 x/y 已丢，只用第一点 |
| `text.rotate` | 字形 rotate 已丢 |
| `text.shear` | 文本含剪切，只保留 TRS |
| `font.fallback` | 未指定 font-family，用 PingFang SC |
| `tag.unknown` | 未支持标签 |
| `shape.empty` | 零几何 |
| `path.conic` | conic 已近似为 cubic |
| `paint.unresolved` | fill/stroke IRI 无效或 pattern |
| `stroke.dash` | dash 已丢 |
| `gradient.stops` | stop 不足 |
| `gradient.focal` | 径向焦点已丢 |
| `gradient.spread` | pad 以外已丢 |
| `clip.unsupported` | clip 过复杂 |
| `mask.skipped` | `mask` 未导入 |
| `filter.skipped` | 滤镜未导入 |
| `use.missing` / `use.cycle` | use 失败 |
| `image.external` | 外部图未导入 |
| `image.decode` | data URI 解码失败 |

解析级失败（不是 SVG、根不是 `svg`、尺寸为 0）只走 `Expected` 错误串，不进 `ImportResult`。

---

## 10. 测试

`svg_import_tests`，夹具放 `tests/import/svg/fixtures/`。

最低覆盖：

1. **解析失败**：空缓冲、非 XML、根不是 svg → `!hasValue()`
2. **单 path + fill/stroke**：一层 Shape；Network 边数与闭合；Fill/Stroke 颜色与宽度
3. **rect / circle / ellipse**：都是 `ShapePath`，不是 Rect/Ellipse 几何；闭合 Network
4. **`<g transform>` + 子 path**：Group + `parentId`；子层 R/S 与矩阵一致；`anchor` = 该层 AABB 中心；`T(pos)*R*S*T(-anchor)` 等于 SVG 矩阵（允许 1e-3）
5. **viewBox 非 0 原点**：根 Group 带映射变换；`sourceWidth/Height` 有值；当前合成宽高不变
6. **继承 fill**：子 path 无 fill 时吃到组上的 fill
7. **linearGradient**：`StylePaintMode::Gradient`，stops ≥ 2，start/end 在 AABB 空间
8. **`<use>`**：展开后层数 ≥ 2，改源 defs 不影响（defs 本就不在图层里）
9. **`<text>`**：一层 `LayerType::Text`；`text` / `fontSize` / `align` / Fill 颜色与源一致；`boxTextMode == false`；无 `text.skipped`
10. **display:none**：该子树无层
11. **line**：开口 Network + 仅 Stroke
12. **data URI image**：Image 层 + Asset + `embeddedImages` 非空
13. **ImportSvgInto + undo**：插入现有合成后层数增加；`undo` 一次后层与 asset 复原；合成尺寸不变
14. **中心锚点**：轴对齐 rect `(10,20,40,30)` 无额外 transform 时 `anchor ≈ (30, 35)`，`position ≈ (30, 35)`
15. **渲染对照（人工）**：kitchen-sink SVG 经 `ImportSvgInto` → `SceneEvaluator` → `PlayCommands` 渲一帧，写出 WebP（对标 `ColorSourceEffectTest` 的 `SaveWebp`）；另存一份 `SVGDOM::render` 参考图。CI 只断言画面非空，**不做**像素对拍

模型字段仍由 `svg_import_tests` 断言。需要时可用 `SceneEvaluator` 做包围盒回归，但不作为 v1 门禁。

---

## 11. 文件与构建改动清单

| 文件 | 动作 |
|---|---|
| `CMakeLists.txt` | `TGFX_CMAKE_ARGS` 增加 `-DTGFX_BUILD_SVG=ON`；`add_subdirectory(src/import/svg)` |
| `src/import/svg/*` | 新建库 |
| `include/MotionStudio/import/svg/SvgImporter.h` | 公开 API |
| `tests/import/svg/*` | 模型测与夹具 |
| `adapter/tgfx/tests/SvgImportRenderTest.mm` | 导入后渲一帧落 WebP（人工核对） |
| `docs/architecture.md` | 目录树补 `import/svg`（实现阶段改） |
| `docs/README.md` | 链到本 spec |

**不改**：Core 模型、schema、bridge、App。

---

## 12. 实现顺序（仅规划，本文件不是 plan）

1. CMake：`TGFX_BUILD_SVG=ON` + 空库能链 tgfx `SVGDOM::Make`
2. `BuildSvgLayers` 失败路径 + `sourceWidth/Height`
3. Path → VectorNetwork + 单 path 夹具
4. 基本形状 + 组树 + 中心锚点分解
5. `ImportSvgInto` + 一次 undo
6. 样式继承 + 纯色 Fill/Stroke
7. 渐变 + use + image data URI
8. clip 有限支持与其余 diagnostic
9. `<text>` → 点文本 Text 层
10. 导入后渲染对照：落 WebP 供人工核对

正式分 Task 的实现计划等本 spec 评审后再写。

---

## 方案取舍（已选第一项）

**解析**

1. **SVGDOM 遍历 + 自建几何**（采用）：typed 属性、CSS class、id mapper 现成；不碰私有 RenderContext
2. XMLDOM + 自写 SVG：和 PAGX 导入重复，单位/path/继承成本高
3. `SVGDOM::render` 再矢量化：丢掉树和可编辑拓扑

**几何**

1. **一律 VectorNetwork**（采用）：与「二次编辑」一致
2. rect/circle 保持参数几何：和需求冲突；圆角/变换残差还要特判

**库位置**

1. **`src/import/svg` 对标 `src/export/pag`**（采用）
2. `adapter/svg`：也说得通，但导入是模型转换不是 RenderAdapter
3. 塞进 core：会让 core 链 tgfx，破坏「Core 不知渲染后端」

**锚点**

1. **局部 AABB 中心 + 补偿 position**（采用）：旋转/缩放绕视觉中心；路径坐标仍是 SVG 局部
2. 锚在 (0,0)（SVG 原点）：和面板/选中框中心不一致
3. 把几何平移到原点使 `anchor=(0,0)`：视觉等价，但改写路径点，钢笔数字对不上源 SVG

**插入**

1. **图层树 + `ImportSvgInto(Document, UndoManager, compositionId)`**（采用）
2. 新建 Document：调用方还要再搬一层，且无法对「当前合成」一次 undo
3. 只返回 layers、由 App 自己拼命令：undo 语义会分叉，库测盖不住插入
