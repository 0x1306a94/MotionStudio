# Figma 式选中框 Resize + 框文本模式 — 设计说明

日期：2026-07-30  
状态：已确认，实现计划见 `docs/superpowers/plans/2026-07-30-figma-style-resize-box-text.md`  
分支：`feature/0x1306a94_text_layer`

## 目标

对齐 Figma 的选中框语义，并简化文本框模型：

1. **选中框角点/边中点永不写 `transform.scale`**；缩放只能在属性面板手动改
2. 单选 / 多选均保留 resize；多选相对选区包围盒比例变换各对象
3. Image：手柄一律改 `image.size`；删除 container / transform 模式开关
4. Shape：手柄改写路径顶点（几何真变大），不改 scale
5. Text：`content.size` 始终为真实固定框；废弃 `autoHeight` 与虚拟测高；新增 `boxTextMode`

## 非目标

- 属性面板以外的临时「按住修饰键改 scale」手势
- Precomp / Null 层的特殊 resize 语义（无内容尺寸时：多选中只更新 position，或跳过无 bounds 对象）
- Lottie 导出对 `boxTextMode` 的完整映射（可后续跟进）
- 改动旋转 / 移动 / 锚点拖动手势（保持现状）

---

## §1 选中框交互

### 行为

| 手势 | 行为 |
| --- | --- |
| 移动 | 只改 `transform.position`（现有） |
| 旋转 | 只改 `transform.rotation`（+ 必要时 position，现有） |
| 锚点 | 改 `anchor` + 补偿 `position`（现有） |
| 角点 / 边中点 | **只**改内容尺寸或路径几何 + 补偿 position/anchor；**禁止**写 `transform.scale` |

对角/对边固定、可越过对边翻折：沿用当前容器缩放的 signed min/max 逻辑。

### 按图层类型（单选）

| 类型 | Resize 写入 |
| --- | --- |
| Image | `image.size` + 等比 `anchor` + 补偿 `position` |
| Text | `content.size` + 等比 `anchor` + 补偿 `position` |
| Shape | 内容路径 + mask 路径顶点局部仿射 + 补偿 `position` / `anchor`（见 §2） |

### 多选

相对**选区包围盒**（现有 axis-aligned union 或 oriented box）：

- 每个选中层的 `position` 相对包围盒 pivot 做 sx/sy
- Image / Text：各自 `size` 乘以 |sx| / |sy|（与翻折规则一致），anchor 等比
- Shape：路径顶点相对包围盒做同样仿射
- **不**写任何层的 `transform.scale`

### UI 删除

- 删除 `ImageResizeMode`（container / transform）分段控件与 `EditorState.imageResizeMode`
- `FreeTransformDrag` 不再依赖 `imageResizeMode`；有 content size 的层走容器/几何 resize

### 伪代码

```text
onHandleResize(kind, layers, scaleX, scaleY, pivot):
  for layer in layers:
    never write transform.scale
    switch layer.type:
      IMAGE: resizeBox(image.size, ...)
      TEXT:  resizeBox(content.size, ...)
      SHAPE: resizePathVertices(...)
      default: // 无几何尺寸时仅按包围盒更新 position（若有）
```

---

## §2 形状路径 Resize

### 单选

拖动手柄时，在图层局部坐标对**内容路径与 mask 路径**的全部顶点做同一局部仿射（可翻折）：

```text
newPoint = fixedLocalPivot + (point - fixedLocalPivot) * (scaleX, scaleY)
// scale 可负 → 翻折；extent clamp 最小 1px（与容器逻辑一致）
```

- 写回路径数据（undo 可 merge，拖动中合并为一个单元）；一次拖动内 shape 路径与 mask 路径同一 undo 单元
- `transform.scale` 不变
- `position` / 等比 `anchor`：对边/对角钉在 `pivotScene`（与容器 resize 同一套补偿）

### 多选

每个 shape 的内容路径与 mask 路径顶点相对**选区包围盒**在场景空间（或统一到各层局部）做同一仿射；并更新 `position`。实现时优先：把包围盒变换分解为「每层局部的线性部分 + 平移」，避免重复累计误差。

### 命令

新增或扩展现有 path 编辑命令，例如 `ResizeShapePathsCommand`：

- 持有 layerId → 旧/新路径快照（或 delta）
- `mergeWith`：同一次 drag merge group 内合并
- 不改 scale 属性

---

## §3 文本：`boxTextMode` 替换 `autoHeight`

### 模型

```cpp
class TextContent : public LayerContent {
    Animatable<std::string> text{...};
    std::string fontFamily{"PingFang SC"};
    std::string fontStyle{};
    Animatable<float> fontSize{48.0f};   // 用户设定的字号上限
    Animatable<Vec2> size{Vec2{400, 120}}; // 始终真实固定框
    bool boxTextMode = false;             // 新增；默认关
    TextAlign align = TextAlign::Left;
    // 删除: bool autoHeight
};
```

| `boxTextMode` | 排版 |
| --- | --- |
| `false` | 按 `size.x` 换行；超出部分 **clip** 到 `size` 矩形；**不**缩字号 |
| `true` | 按框宽换行；高度不够则二分缩字号（上限 `fontSize`）直到完整落入框内 |

### 去掉虚拟框

- 删除 Bridge/Canvas 在 `autoHeight` 下测高写回 `hitSize` 的逻辑
- 选区 / hit bounds = 求值后的 `content.size`（与 transform 组合），不再用布局测高覆盖

### 渲染 / TextLayout

- `DrawText` / adapter：`autoHeight` 参数改为 `boxTextMode`（或等价：`clipOnly` vs `shrinkToFit`）
- 现有 `TextLayout`：`boxHeight = nullopt` 曾表示 auto 高；改为：
  - `boxTextMode == false`：有 `boxHeight`，但 **禁用** shrink，只 clip
  - `boxTextMode == true`：有 `boxHeight`，启用 shrink（现有二分逻辑）

### 序列化

- JSON 字段用 `boxTextMode`；**不**读、不迁移旧 `autoHeight`
- 删除所有 `autoHeight` 相关模型 / 命令 / Bridge / 适配器 / 测试代码（直接删，不做兼容分支）

Undo：删除 `SetTextAutoHeightCommand`，新增 `SetTextBoxTextModeCommand`。

Bridge / Inspector：`ms_command_set_text_auto_height` → `ms_command_set_text_box_text_mode`；UI 文案「框文本模式」。

新建文本层：固定 `size`（如 400×120），`boxTextMode = false`，anchor 居中。

---

## §4 实现面与测试

### 主要改动文件（预期）

- `apps/.../Canvas/FreeTransformDrag.swift` — 去掉 scale 写入；多选几何 resize
- `apps/.../Editor/*` — 删除 ImageResizeMode 控件
- `include/MotionStudio/model/TextContent.h` + 序列化 / undo / bridge
- `src/render/*`、`adapter/tgfx/*`、`adapter/textlayout/*` — DrawText 语义
- 形状路径 resize 命令 + bridge API（若 Swift 需调用）
- 文档：`docs/rendering.md`、文本相关 plan/spec 交叉引用更新

### 测试

- 单选 Image/Text：对角固定、边拖、翻折；结束后 `transform.scale` 与拖前一致
- 单选 Shape：内容路径与 mask 路径顶点包围盒同步变化，`scale` 不变
- 多选混合类型：相对包围盒比例；各类型写入正确字段（shape 含 mask）
- 文本：`boxTextMode` 关 clip / 开 shrink；JSON round-trip（无 `autoHeight`）
- 回归：属性面板改 scale 仍生效；旋转/移动/锚点不变

---

## 已确认决策摘要

1. 手柄不改 scale；scale 仅属性面板
2. 多选 resize 保留，按包围盒改 position + size/路径（方案 A）
3. 形状本期完整做路径顶点改写（内容路径 + mask 路径同步）
4. `autoHeight` 直接删除，新增 `boxTextMode`（开=换行+缩字；关=换行+clip）；反序列化不处理旧 `autoHeight` 字段
5. 删除图片 container/scale 模式 UI
