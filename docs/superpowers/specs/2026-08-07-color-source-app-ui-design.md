# Color Source App UI 与预览接线 — 设计说明

日期：2026-08-07  
状态：设计已确认，待写实现计划  
前置：[Color Source Core 存储](2026-08-07-color-source-core-storage-design.md)（已实现）  
关联：[ColorSourceEffect 与 RenderCache](../../color-source-effect.md)、[数据模型](../../data-model.md)

## 目标

在 App 中完成过程色的**库管理、源码/scheme 编辑、Fill/Stroke 绑定与调参、工程包持久化**，并把预览管线接到 `ColorSourceEffect`，使编辑结果能在画布上看到。

## 已锁定决策

| 项 | 选择 |
|---|---|
| 范围 | 全量：包 `shader.json` + ProjectPanel 库 + Sheet 编辑 + Inspector + 预览接线 |
| 架构 | 分层扩展现有路径（Bridge → 包 → UI → SceneEvaluator/adapter） |
| 源码编辑入口 | 库只列表；编辑一律 **Sheet**（对标 Assets 简洁度，不新开常驻代码面板） |
| Sheet 布局 | 对标 Shadertoy：上 Inputs、下 `mainImage` |
| Inputs | 按 **std140 声明顺序**展示；内置在前 + 注释；用户项只读卡片，增删/编辑靠按钮 |
| 编译失败 | tgfx 不绘制即可；**v1 不做**诊断回传、品红占位、Inspector 错误态 |
| 内置 uniform | 不进 `ShaderDefinition.uniforms`；Sheet 只读展示；adapter 每帧注入 |
| 用户 scheme 维护 | 手工（Add/Edit/Delete），不解析 GLSL 推断 |
| 调色 | 仍靠多 Fill + `blendMode`；不用 color 乘 shader |

## 非目标

- 语法高亮 / 补全 / 实时小窗预览
- Int / Mat / Sampler / iChannel 编辑
- 从 GLSL 自动生成 scheme
- Lottie / PAG 导出过程色
- 编译错误诊断 UI
- 拖拽 shader 到画布、库多选、缩略图预览

## 方案取舍

选用 **分层扩展现有路径**：Core 已有模型与 undo；Bridge 补薄 API；包层成对读写 `shader.json`；UI 对标 Assets + FillsInspector；求值快照进 `Paint`，adapter 消费 `ColorSourceEffect`。

不采用：厚 Swift 自管 scheme 状态（与 Core undo/Realign 重复）；预览与 UI 拆成互不接通的两期（本轮需要画布可见）。

---

## 1. 架构与数据流

```
ProjectPanel (Shaders 列表)
        │ New / Rename / Delete / Edit Source
        ▼
ShaderEditorSheet  ←→  MotionDocumentCore (bridge)
  Inputs(std140) + mainImage
        │
        ▼
Fills/StrokesInspector
  Color|Shader 切换 → 选 shader → 编辑 uniformValues（可关键帧）
        │
MotionProjectDocument
  document.json + shader.json + assets/
        │
Canvas drawFrame
  SceneEvaluator → SceneState（Paint 含 shader 求值快照）
       → BuildCommands → TgfxCanvasAdapter → ColorSourceEffect
  源码错误：tgfx 不画该 paint；无额外 UI
```

| 层 | 做什么 | 不做什么 |
|---|---|---|
| Core（已有） | `Document.shaders`、Fill XOR、undo、序列化 | UI / GPU |
| Bridge | shader CRUD、paintMode、uniform 读写、`serialize/deserializeShaders` | 业务策略 |
| App 包 | `shader.json` 与 `document.json` 成对读写 | 解析 GLSL |
| App UI | 列表 + Sheet + Inspector | 直接碰 adapter |
| Render / adapter | 求值快照 → `ColorSourceEffect`；定义变更后 `invalidate` | 改 Document |

---

## 2. ShaderEditorSheet

对标 Shadertoy 编辑区。

```
┌ Sheet ──────────────────────────────────────┐
│ [Name]                         [Cancel][Save] │
│ ┌ Inputs（只读列表，std140 顺序）─────────────┐ │
│ │ // Built-ins (injected each frame)          │ │
│ │ vec3  iResolution;  // xy = sourceBounds px │ │
│ │ float iTime;        // seconds              │ │
│ │ float iTimeDelta;                           │ │
│ │ int   iFrame;                               │ │
│ │ float iFrameRate;                           │ │
│ │ // User uniforms                            │ │
│ │ float rippleCount;               [Edit][−]  │ │
│ │ vec4  tint;                      [Edit][−]  │ │
│ │                          [+ Add Uniform]    │ │
│ └─────────────────────────────────────────────┘ │
│ ┌ mainImage ──────────────────────────────────┐ │
│ │ vec4 mainImage(vec2 uv) { … }   // 可编辑   │ │
│ └─────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────┘
```

### 2.1 行为

| 项 | 约定 |
|---|---|
| 入口 | ProjectPanel「Edit Source」；Fill/Stroke 行「Edit Source」打开**同一** Sheet（同一 `shaderId`） |
| Inputs | **不可直接改文本**；顺序 = 内置固定前缀（与 `PrependShadertoyBuiltinUniforms` 一致）+ `ShaderDefinition.uniforms` |
| 内置 | 只读 + 注释；不可删 |
| 用户 uniform | `+`：name + format（v1：`Float` / `Float2` / `Float3` / `Float4`）；`−` 删除；`Edit` 改 name/format |
| mainImage | 自由文本；新建默认模板，例如 `vec4 mainImage(vec2 uv) { return vec4(uv, 0.0, 1.0); }` |
| Save | 一次 `UpdateShaderDefinitionCommand`（name + mainImage + uniforms）；Core Realign 各引用处 `uniformValues` |
| Cancel | 丢弃草稿，不写 Core |
| 编译错 | 仍可 Save；画布侧 tgfx 不画 |

### 2.2 Inputs 展示与 std140

- 展示顺序与最终 `layout(std140) uniform UniformBlock` 成员声明顺序一致：内置在前，用户在后。
- 内置注释说明语义（分辨率、播放时间、帧号、帧率等）；与 [color-source-effect.md](../../color-source-effect.md) §3 对齐。
- v1 可不在 UI 画出 padding 字节间隙；以「类型 + 名 + 注释」行列表为准，顺序正确即可。

---

## 3. ProjectPanel 库 + Inspector

### 3.1 ProjectPanel — Shaders 段

- 位置：Assets 与 Compositions **之间**。
- 列表：`shaderIDs` → 图标 + `name`；空态 `ContentUnavailableView`。
- 操作：
  - **New Shader** → 插入默认模板 → 打开 Sheet
  - 行：**Edit Source**、**Rename**（Sheet 顶栏或行内）、**Delete**
  - Delete：Core 拒绝（仍被引用）→ Alert；成功则刷新列表
- **不做**：拖拽到画布、多选、缩略图

### 3.2 Fills / Strokes Inspector

在现有 blend / 菱形 / 删除之外：

| 控件 | 行为 |
|---|---|
| Paint 模式 | `Color` \| `Shader` → `setStylePaintMode` / Bind·Clear |
| Color 模式 | 保持现有 `ColorPicker` + 颜色关键帧菱形 |
| Shader 模式 | 隐藏 ColorPicker；**Menu 选 shader**（对标 ImageLayer 绑 asset）；旁路 **Edit Source** |
| Uniforms | 已绑定则行下展开：按 scheme 生成控件（Float→数值；Float2/3→多字段；Float4→`ColorPicker`）；path = `styles[N].uniformValues.<name>`；菱形关键帧复用现有 `perform` / merge |
| 未绑定 | Menu「Select Shader…」；无 uniform 区 |

Stroke 行同构（width / trim 等保留）。

---

## 4. Bridge、包读写、预览接线

### 4.1 Bridge

Swift `MotionDocumentCore` 增补（名称示意）：

| 能力 | API 方向 |
|---|---|
| 库 | `shaderCount` / id 列表 / `shaderName` / `shaderMainImage` / uniform decls 读取 |
| 写库 | `addShader`（模板）/ `updateShader` / `removeShader` / `renameShader` |
| 序列化 | `serializeShaders()` / `loadShaders(json:)`（与 document `serialize` 分开） |
| 样式 | `stylePaintMode` / `setStylePaintMode` / `styleShaderID` / `bindStyleShader` / `clearStyleShader` |
| Values | 复用现有 evaluate / setStatic / addKeyframe，path = `styles[N].uniformValues.<name>` |

删失败返回 false 或错误串 → Alert。Undo 文案走现有 `perform("…")`。

### 4.2 包读写

```
*.motionstudio/
  document.json
  shader.json      ← 新增
  assets/
```

- **Load**：有 `shader.json` 则 `loadShaders`；无则空库；再加载 document。
- **Save**：`serialize()` + `serializeShaders()` 成对写入；可写 `shaders: []`。
- shader 变更同样 `markDirty`。

### 4.3 预览接线

当前 `Paint` 仅纯色。扩展为求值后自包含快照（adapter **不**回查 Document）：

```
Paint
  paintMode
  color                         // Color 模式
  shader?                       // Shader 模式
    shaderId
    mainImage
    uniforms[]                  // scheme decls
    evaluated values            // 本帧 UniformData::setData
```

- `SceneEvaluator`：`paintMode == Shader` 时查 `Document.shaders`，求值 `uniformValues`，写入快照；**找不到 id → 该 style 不产出绘制项**。
- `BuildCommands`：`DrawPath` / `StrokePath` 携带扩展后的 `Paint`。
- `TgfxCanvasAdapter`：Shader 模式 → `ColorSourceEffect::Make` + `setData` + `setFrameContext`（播放头 / 帧率）+ `setShader`；Color 模式不变。
- **Pipeline 失效**：改 `mainImage` / decls 后须 `invalidateColorSourcePipeline(shaderId)`。v1：canvas 在 content revision 变化时按本次快照 id invalidate，或定义更新后按 id invalidate。
- 源码编译失败：tgfx 不绘制；无 UI 特殊处理。

### 4.4 测试

- Bridge：shader CRUD、paintMode round-trip、shaders JSON。
- Core render：Shader paint 进入 `SceneState`；缺 id 跳过。
- Adapter：既有 ColorSourceEffect 测试 + 经 `drawPath` 的 shader paint 烟测。
- App：`FileWrapper` 包读写 `shader.json`；UI 以手动验证为主。

---

## 5. 默认模板（新建 shader）

```glsl
vec4 mainImage(vec2 uv) {
    return vec4(uv, 0.0, 1.0);
}
```

- 默认 `name`：如 `Shader` / `Shader 2`（实现时与现有「未命名层」命名风格对齐）。
- 默认 `uniforms`：空；用户在 Sheet Inputs 区添加。

---

## 6. 实现分期建议（plan 可拆 Task）

1. Bridge + 包 `shader.json` 读写  
2. ProjectPanel 列表 + Sheet（Inputs + mainImage）  
3. Inspector：paintMode / 绑定 / uniformValues  
4. `Paint` 快照 + SceneEvaluator / CommandBuilder  
5. TgfxCanvasAdapter ↔ ColorSourceEffect + invalidate  

依赖顺序：1 → 2/3 可并行；4 → 5；4/5 完成前 UI 可写数据但画布无过程色。

---

## 7. 成功标准

- 新建 / 编辑 / 删除 shader；删除被引用时有提示且不删。
- 工程打开/保存后 `shader.json` 与引用一致。
- Fill/Stroke 可在 Color 与 Shader 间切换，绑定后可调 uniform（含关键帧）。
- 合法 `mainImage` 在画布上随播放头更新（`iTime` 等）；非法源码不画、不崩溃、无强制错误 UI。
