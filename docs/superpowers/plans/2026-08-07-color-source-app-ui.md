# Color Source App UI 与预览接线 — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 打通过程色从工程包 ↔ Bridge ↔ ProjectPanel/Sheet/Inspector 到画布预览（`ColorSourceEffect`），使合法 `mainImage` 可编辑并在播放头下可见。

**Architecture:** 分层扩展现有路径。Core 模型/undo 已就绪；本 plan 补 Bridge + `shader.json` 包读写 + SwiftUI（Shaders 列表、Shadertoy 式 Sheet、Fill/Stroke paintMode/uniforms）+ 扩展 `Paint` 求值快照 + `TgfxCanvasAdapter` 接线。编译失败不做 UI 诊断（tgfx 不画即可）。

**Tech Stack:** C++17 Core/bridge、GoogleTest、adapter tgfx、SwiftUI App（`MotionDocumentCore` / `UIDocument`）。

**Spec:** `docs/superpowers/specs/2026-08-07-color-source-app-ui-design.md`  
**前置:** `docs/superpowers/specs/2026-08-07-color-source-core-storage-design.md`（已实现）

## Global Constraints

- 分支：留在当前 feature 分支（如 `feature/runtime_shader`）；未经明确要求不得往 `master` 提交。
- **自动 commit：** 每完成一个 Task 必须提交；**提交前先**把本 plan 对应 checkbox 改为 `[x]`、更新 `**Status:**`，再 `git add` 代码与本 plan 一并 commit。
- Commit 信息：英语、≤120 字符、句号结尾、句中无其他标点。
- 按本 plan 实现时：每完成一个 Step 立刻勾选；未同步 plan 视为该步未完成。
- Core / bridge / adapter 宣称完成前优先 ASan：`cmake -B build -G Ninja -DMOTIONSTUDIO_ENABLE_ASAN=ON && cmake --build build`。
- App / Xcode：优先 Xcode MCP `BuildProject`；不可用再 `xcodebuild`（见 `AGENTS.md`）。
- 遵守编码规范（禁异常、禁 `dynamic_cast`、错误用 `Expected`）；Bridge 只做类型转换与命令转发。
- **不做：** 语法高亮、编译诊断 UI、sampler/iChannel、Lottie/PAG 过程色导出、从 GLSL 推断 scheme。
- 新建 shader 默认 `mainImage`：

```glsl
vec4 mainImage(vec2 uv) {
    return vec4(uv, 0.0, 1.0);
}
```

- 内置 uniform 顺序（与 `PrependShadertoyBuiltinUniforms` 一致，不进 scheme）：`iResolution`(Float3)、`iTime`(Float)、`iTimeDelta`(Float)、`iFrame`(Int)、`iFrameRate`(Float)。

---

## 文件对照

| 文件 | 职责 |
|---|---|
| `bridge/include/motionstudio_bridge.h` + `bridge/src/common/motionstudio_bridge_shader.cpp`（新建） | shader CRUD / paintMode / serializeShaders C API |
| `bridge/tests/BridgeTest.cpp`（或新建 `BridgeShaderTest.cpp`） | Bridge 往返测试 |
| `apps/.../Model/MotionDocumentCore.swift` | Swift facade |
| `apps/.../Bridge/PropertyPath.swift` | `styles[N].uniformValues.<name>` 辅助 |
| `apps/.../Document/MotionProjectDocument.swift` | 包读写 `shader.json` |
| `apps/.../ProjectPanel/ProjectPanelView.swift` | Shaders 段 |
| `apps/.../ProjectPanel/ShaderEditorSheet.swift`（新建） | Inputs(std140) + mainImage Sheet |
| `apps/.../Inspector/FillsInspector.swift` / `StrokesInspector.swift` | paintMode / 绑定 / uniforms |
| `apps/.../Editor/...`（挂 Sheet / New Shader 回调） | 接线入口 |
| `include/MotionStudio/render/Paint.h` + 求值辅助 | `StylePaintMode` + `ShaderPaint` 快照 |
| `include/MotionStudio/render/SceneState.h` | 帧上下文（供 iTime） |
| `src/render/SceneEvaluator.cpp` | Shader 模式求值；缺 id 跳过 |
| `tests/render/*` | SceneState 含 shader paint |
| `adapter/tgfx/src/TgfxCanvasAdapter.cpp` + `.h` | ColorSourceEffect 接线 + invalidate |
| `adapter/tgfx/src/RenderCache.*` | 可选：按源码指纹失效 pipeline |
| `docs/color-source-effect.md` / spec 状态 | 同步「已接线」 |

---

### Task 1: Bridge API + 包 `shader.json`

**Status:** ✅ Done

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`
- Create: `bridge/src/common/motionstudio_bridge_shader.cpp`（并挂入 bridge CMake）
- Modify: `bridge/tests/BridgeTest.cpp`（或 Create: `bridge/tests/BridgeShaderTest.cpp`）
- Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Document/MotionProjectDocument.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Bridge/PropertyPath.swift`（uniform path 辅助，可与 Task 3 一并，但 path 常量本 Task 先加）

**Interfaces:**
- Consumes: Core `AddShaderCommand` / `RemoveShaderCommand` / `UpdateShaderDefinitionCommand` / `SetStylePaintModeCommand`；`Serializer::serializeShaders` / `deserializeShaders`；`FindShader` / `ShaderIsReferenced`；`BindShaderPaint`
- Produces（C ABI 示意，实现时保持与现有 `ms_document_asset_*` 风格一致）：

```c
typedef CF_CLOSED_ENUM(int, MS_PAINT_MODE) {
    MS_PAINT_MODE_INVALID = -1,
    MS_PAINT_MODE_COLOR = 0,
    MS_PAINT_MODE_SHADER = 1,
};

typedef CF_CLOSED_ENUM(int, MS_UNIFORM_FORMAT) {
    MS_UNIFORM_FORMAT_INVALID = -1,
    MS_UNIFORM_FORMAT_FLOAT = 0,
    MS_UNIFORM_FORMAT_FLOAT2 = 1,
    MS_UNIFORM_FORMAT_FLOAT3 = 2,
    MS_UNIFORM_FORMAT_FLOAT4 = 3,
    // v1 UI 只暴露以上；其它 format 枚举值可按 UniformFormat 顺序继续对齐以便将来扩展
};

int ms_document_shader_count(MSDocument *document);
uint64_t ms_document_shader_id_at(MSDocument *document, int index);
char *ms_document_shader_name(MSDocument *document, uint64_t shaderId);       // ms_string_free
char *ms_document_shader_main_image(MSDocument *document, uint64_t shaderId); // ms_string_free
int ms_document_shader_uniform_count(MSDocument *document, uint64_t shaderId);
char *ms_document_shader_uniform_name_at(MSDocument *document, uint64_t shaderId, int index);
MS_UNIFORM_FORMAT ms_document_shader_uniform_format_at(MSDocument *document, uint64_t shaderId, int index);

// Returns new shader id; name/mainImage 用默认模板；uniforms 空。
uint64_t ms_document_add_shader(MSDocument *document, const char *name);
// Full replace of name/mainImage/uniforms（一次 undo）。uniformsJson 示例：
// [{"name":"a","format":"float","count":1}, ...]  与 shader.json 单条 uniforms 同形。
bool ms_document_update_shader(MSDocument *document, uint64_t shaderId,
                               const char *name, const char *mainImage,
                               const char *uniformsJson);
bool ms_document_remove_shader(MSDocument *document, uint64_t shaderId); // false if referenced
bool ms_document_rename_shader(MSDocument *document, uint64_t shaderId, const char *name);

char *ms_document_serialize_shaders(MSDocument *document); // ms_string_free
// Package open: load document.json + shader.json **together** into one Document
// (shaders merged before the handle is returned; then ValidateShaderReferences).
// shadersJson may be NULL / length 0 → empty library.
MSDocument *ms_document_load_json_with_shaders(const char *documentJson, size_t documentLength,
                                              const char *shadersJson, size_t shadersLength,
                                              char **errorOut);

MS_PAINT_MODE ms_layer_style_paint_mode_at(MSDocument *document, uint64_t layerId, int index);
uint64_t ms_layer_style_shader_id_at(MSDocument *document, uint64_t layerId, int index);
bool ms_document_set_style_paint_mode(MSDocument *document, uint64_t layerId, int index,
                                     MS_PAINT_MODE mode, uint64_t shaderId);
```

- Swift：`MotionDocumentCore` 镜像上述 API；`serializeShaders() throws -> Data`；`init(documentJSON:shadersJSON:)`（或缺省空库）走 `ms_document_load_json_with_shaders`。
- 包：`Package.shaderFilename = "shader.json"`。
- **加载约定：** 打开工程时 `document.json` 与 `shader.json` **一次**合并进内存 `Document`（先 deserialize document，再赋 `shaders`，再 `ValidateShaderReferences`，失败则整个打开失败）。禁止「先只有 document、稍后补 shaders」的中间态暴露给 UI。保存仍写两个文件。

- [x] **Step 1: 写 Bridge 失败测试**
- [x] **Step 2: 跑测试确认失败**
- [x] **Step 3: 实现 C Bridge + Swift facade**
- [x] **Step 4: 包读写 `shader.json`**
- [x] **Step 5: 跑通 Bridge 测试并 commit**

---

### Task 2: ProjectPanel Shaders 列表 + ShaderEditorSheet

**Status:** ✅ Done

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/ProjectPanel/ProjectPanelView.swift`
- Create: `apps/MotionStudioApp/MotionStudioApp/ProjectPanel/ShaderEditorSheet.swift`
- Modify: 挂载 `ProjectPanelView` 的 Editor 侧（如 `EditorViewController+SidePanels.swift` 或现有传 `importImage` 的同类回调），以 present Sheet / Alert
- 将新 Swift 文件加入 Xcode 工程（`MotionStudioApp.xcodeproj` / 同步工程文件）

**Interfaces:**
- Consumes: Task 1 的 `shaderIDs` / `addShader` / `updateShader` / `removeShader` / `shaderMainImage` / uniform getters
- Produces: `ShaderEditorSheet(core:shaderID:onDismiss:)`；`ProjectPanelView` 增加 `onEditShader: (UInt64) -> Void`、`onNewShader: () -> Void` 等回调

- [x] **Step 1: ProjectPanel Shaders 段**

对标 Assets：插在 Assets 与 Compositions 之间。

```swift
// 结构示意
HStack {
    Text("Shaders").font(.caption).foregroundStyle(.secondary)
    Spacer()
    Button("New Shader", action: onNewShader).font(.caption).buttonStyle(.borderless)
}
if shaders.isEmpty {
    ContentUnavailableView("No Shaders", systemImage: "wand.and.stars",
                           description: Text("Create a process color shader."))
} else {
    ForEach(shaders, id: \.self) { id in
        // name + context menu: Edit Source / Delete
    }
}
```

- `onNewShader`：`perform("Add Shader") { let id = core.addShader(name: uniqueName); … }` 后打开 Sheet。
- Delete：`if !core.removeShader(id) { show alert "Still referenced by a Fill or Stroke." }`。

- [x] **Step 2: ShaderEditorSheet（Shadertoy 布局）**

草稿态 `@State`：`name`、`mainImage`、`uniforms: [(name, format)]`；打开时从 core 灌入。

布局：
1. 顶栏 Name + Cancel / Save  
2. **Inputs**（只读行，顺序固定）：
   - 注释头 `// Built-ins (injected each frame)`
   - 五行内置（类型名用 GLSL：`vec3`/`float`/`int`）+ 短注释（与 spec / `color-source-effect.md` §3 一致）
   - 注释头 `// User uniforms`
   - 用户行：`format name` + Edit + −  
   - `+ Add Uniform` → 弹 name + format（仅 Float/Float2/Float3/Float4）
3. **mainImage** `TextEditor`（等宽字体）

Save：

```swift
perform("Update Shader") {
    core.updateShader(id: shaderID, name: name, mainImage: mainImage, uniforms: uniforms)
}
```

Cancel：dismiss，不写 core。

内置注释文案（锁定）：

| name | 注释 |
|---|---|
| iResolution | xy = source bounds px, z = 1 |
| iTime | seconds |
| iTimeDelta | seconds since previous draw |
| iFrame | frame index |
| iFrameRate | composition fps |

- [x] **Step 3: 接线 present Sheet**

Editor 用 `.sheet` / `UIHostingController` 与现有 Inspector 嵌入方式一致；Fill 行 Edit Source（Task 3）复用同一 Sheet。

- [x] **Step 4: 手动验证 + commit**

用 Xcode 跑 App：New Shader → 改 mainImage/加 uniform → Save → 列表名更新；删未引用成功；绑引用后删应 Alert（可先用临时 bridge 调 paintMode 测，或等 Task 3）。

```bash
git add apps/MotionStudioApp docs/superpowers/plans/2026-08-07-color-source-app-ui.md
git commit -m "Add shader library panel and Shadertoy style editor sheet."
```

---

### Task 3: Inspector paintMode / 绑定 / uniformValues

**Status:** ✅ Done

**Files:**
- Modify: `apps/.../Inspector/FillsInspector.swift`
- Modify: `apps/.../Inspector/StrokesInspector.swift`
- Modify: `apps/.../Bridge/PropertyPath.swift`
- Modify: `apps/.../Model/MotionDocumentCore.swift`（若缺 `evaluateVec3` / `setStaticVec3` / `addKeyframeVec3`）
- Bridge：若尚无 Vec3 属性读写，补 `ms_document_evaluate_vec3` 等（对标现有 float/vec2/color）

**Interfaces:**
- Consumes: Task 1 paintMode/bind API；Task 2 Sheet 入口
- Produces: Inspector 可切换 Color/Shader、绑 shader、编辑/关键帧 uniforms

- [x] **Step 1: PropertyPath 辅助**

```swift
enum StyleProperty {
    // 现有 color, blendMode, …
    static func uniformValue(_ name: String, styleIndex: Int) -> String {
        "styles[\(styleIndex)].uniformValues.\(name)"
    }
}
```

- [x] **Step 2: 补齐 Vec3 bridge（若缺失）**

Inspector Float3 需要：`evaluateVec3` / `setStaticVec3` / `addKeyframeVec3`。照 `Vec2` 路径复制一套；加最小 bridge 测试或依赖现有 PropertyPath core 测试。

- [x] **Step 3: 改 FillsInspector 行**

每行逻辑：

```text
[PaintMode: Color|Shader] [ColorPicker | Shader Menu + Edit] [blend] [◆] [−]
if Shader && bound:
  For each uniform in scheme:
    control + ◆ keyframe on styles[i].uniformValues.name
```

- Color→Shader：若库空，可先禁用或切模式时提示先 New Shader；若有库，默认绑第一个或弹出 Menu 选择后 `setStylePaintMode(.shader, id)`。
- Shader→Color：`setStylePaintMode(.color, 0)`。
- Shader Menu：`ForEach(core.shaderIDs())`；换绑同样走 `setStylePaintMode(.shader, newId)`（Core 命令会 Bind/Realign）。
- Uniform 控件：Float → `TextField`/`Slider` 数值；Float2/3 → 多字段；Float4 → `ColorPicker`；写路径走现有 `perform` + 有关键帧则 `addKeyframe*` 否则 `setStatic*` + `endMergeGroup`。
- Color 模式保留原颜色菱形；Shader 模式颜色菱形隐藏，各 uniform 自有菱形。

StrokesInspector **同构**（保留 width/trim/position）。

- [x] **Step 4: 手动验证 + commit**

切换模式、绑 shader、拖 float、打关键帧、undo。

```bash
git add apps/MotionStudioApp bridge docs/superpowers/plans/2026-08-07-color-source-app-ui.md
git commit -m "Add Fill and Stroke inspector controls for shader paints."
```

---

### Task 4: `Paint` 快照 + SceneEvaluator

**Status:** 待开始

**Files:**
- Modify: `include/MotionStudio/render/Paint.h`
- Create（可选）: `include/MotionStudio/render/ShaderPaint.h`（若希望 Paint.h 保持精简）
- Modify: `include/MotionStudio/render/SceneState.h`
- Modify: `src/render/SceneEvaluator.cpp`（及 `ApplyLayerStyles`）
- Modify: 所有构造 `Paint{color, …}` 的测试/调用点（补默认 `paintMode = Color`）
- Create/Modify: `tests/render/SceneEvaluatorShaderPaintTest.cpp`（或并入现有 SceneEvaluator 测试）

**Interfaces:**
- Produces:

```cpp
// include/MotionStudio/render/ShaderPaint.h（示意）
struct EvaluatedShaderUniform {
    std::string name;
    ShaderUniformValueKind kind = ShaderUniformValueKind::AnimFloat;
    float floatValue = 0.f;
    Vec2 float2Value{};
    Vec3 float3Value{};
    Color colorValue{1, 1, 1, 1};
};

struct ShaderPaint {
    EntityId shaderId{};
    std::string mainImage;
    std::vector<ShaderUniformDecl> uniforms;       // scheme，用户项（不含内置）
    std::vector<EvaluatedShaderUniform> values;    // 与 uniforms 对齐的本帧值
};

struct Paint {
    StylePaintMode paintMode = StylePaintMode::Color;
    Color color;
    FillRule fillRule = FillRule::NonZero;
    BlendMode blendMode = BlendMode::Normal;
    ShaderPaint shader;  // 仅 paintMode == Shader 时有意义
};

struct SceneState {
    std::vector<EvaluatedLayer> layers;
    int viewportWidth = 0;
    int viewportHeight = 0;
    Color backgroundColor;
    float cornerRadius = 0.0f;
    // PreviewTime 在工程里是「帧」（可分数）；秒 = frames / fps
    float timeSeconds = 0.f;
    int64_t frameIndex = 0;
    float frameRate = 30.f;
};
```

- [ ] **Step 1: 写失败测试**

```cpp
TEST(SceneEvaluatorShaderPaintTest, EvaluatesShaderFillSnapshot) {
    Document doc;
    // 建 composition + shape layer + ShaderDefinition + BindShaderPaint
    // 设 uniform 静态值
    auto state = SceneEvaluator::Evaluate(doc, compositionId, /*frame=*/0);
    ASSERT_TRUE(state.hasValue());
    // 找到 EvaluatedShapeItem：paint.paintMode == Shader
    // paint.shader.mainImage 非空；values 含期望 float
}

TEST(SceneEvaluatorShaderPaintTest, MissingShaderSkipsStyle) {
    // paintMode Shader 但 shaderId 无效 → 该 style 不产生 shapeItems 条目
}
```

- [ ] **Step 2: 跑测试确认失败**

```bash
cmake --build build --target core_tests
./build/tests/core_tests --gtest_filter='SceneEvaluatorShaderPaintTest.*'
```

Expected: FAIL（Paint 无字段或仍为纯色）。

- [ ] **Step 3: 实现求值**

在 `ApplyLayerStyles`：

```cpp
case LayerStyleType::Fill: {
    const auto &fill = static_cast<const FillStyle &>(*style);
    if (fill.paintMode == StylePaintMode::Shader) {
        const ShaderDefinition *def = FindShader(document, fill.shaderId);
        if (!def) break;
        Paint paint;
        paint.paintMode = StylePaintMode::Shader;
        paint.fillRule = fill.fillRule;
        paint.blendMode = fill.blendMode;
        paint.shader.shaderId = def->id;
        paint.shader.mainImage = def->mainImage;
        paint.shader.uniforms = def->uniforms;
        paint.shader.values = EvaluateUniformValues(fill.uniformValues, time);
        // push items…
        break;
    }
    // 现有 Color 路径
}
```

`EvaluateUniformValues`：按 `entries` 调 `evaluatePreview(time)`，写入 `EvaluatedShaderUniform`。  
Stroke 同理。  
`EvaluatePreview` 开头填 `SceneState` 帧字段：

```cpp
const float fps = composition->frameRate.num /
                  static_cast<float>(std::max(composition->frameRate.den, 1u));
state.frameRate = fps;
state.frameIndex = static_cast<int64_t>(time);  // PreviewTime 以帧为单位
state.timeSeconds = fps > 0.f ? static_cast<float>(time / fps) : 0.f;
```

- [ ] **Step 4: 跑测试通过并 commit**

```bash
./build/tests/core_tests --gtest_filter='SceneEvaluatorShaderPaintTest.*'
```

Expected: PASS。修编译期因 `Paint` 聚合初始化被破坏的测试。

```bash
git add include/MotionStudio/render src/render tests/render \
  docs/superpowers/plans/2026-08-07-color-source-app-ui.md
git commit -m "Evaluate shader fill and stroke paints into SceneState snapshots."
```

---

### Task 5: TgfxCanvasAdapter ↔ ColorSourceEffect + invalidate

**Status:** 待开始

**Files:**
- Modify: `adapter/tgfx/include/TgfxCanvasAdapter.h`
- Modify: `adapter/tgfx/src/TgfxCanvasAdapter.cpp`
- Modify: `adapter/tgfx/src/RenderCache.h` / `.cpp`（源码指纹或显式 invalidate）
- Modify: `bridge/src/common/motionstudio_bridge_canvas.cpp`（每帧 `setColorSourceFrameContext`）
- Modify/Create: `adapter/tgfx/tests/ColorSourceEffectTest.mm` 或 `TgfxRenderAdapterTest.cpp` 烟测
- Modify: `docs/color-source-effect.md`（标注预览已接线）
- Modify: `docs/superpowers/specs/2026-08-07-color-source-app-ui-design.md` 状态

**Interfaces:**
- Consumes: Task 4 `Paint` / `SceneState` 帧字段；`ColorSourceEffect::Make` / `setData` / `setFrameContext` / `makeImageShader`
- Produces: `TgfxCanvasAdapter::setColorSourceFrameContext(ColorSourceFrameContext)`；shader 模式 `drawPath`/`strokePath` 使用 ImageShader

- [ ] **Step 1: Frame context 注入**

```cpp
// TgfxCanvasAdapter.h
void setColorSourceFrameContext(ColorSourceFrameContext context);
```

`motionstudio_bridge_canvas.cpp` 在 `PlayCommands` 前：

```cpp
canvas->adapter->setColorSourceFrameContext({
    state.timeSeconds,
    state.frameIndex,
    state.frameRate,
});
```

- [ ] **Step 2: drawPath / strokePath 分支**

伪代码：

```cpp
if (paint.paintMode == StylePaintMode::Shader) {
    // sourceBounds：path 的局部/设备 bounds（与现有 ColorSourceEffect 测试一致，用 path.getBounds()）
    std::vector<Uniform> decls;
    for (const auto &u : paint.shader.uniforms)
        decls.emplace_back(u.name, u.format, u.count);
    maybeInvalidateIfSourceChanged(paint.shader); // 见 Step 3
    auto effect = ColorSourceEffect::Make(paint.shader.shaderId, paint.shader.mainImage,
                                          std::move(decls), bounds, renderCache_.get());
    if (!effect) return; // 或继续不画
    effect->setFrameContext(colorSourceFrameContext_);
    auto *data = effect->getUniformData();
    for (const auto &v : paint.shader.values) {
        // 按 kind setData float / float2 / float3 / float4(color)
    }
    auto shader = effect->makeImageShader();
    if (!shader) return; // 编译失败 → 不画
    tgfxPaint.setShader(shader);
    // 仍可 setAlpha/blend；不必 setColor
    canvas->drawPath(path, tgfxPaint);
    return;
}
// 现有纯色路径
```

Stroke：同样 setShader；注意 stroke 几何仍走现有 Inside/Outside 逻辑。

- [ ] **Step 3: Pipeline invalidate**

在 `RenderCache` 为每个 `shaderId` 存 `sourceKey`（例如 `mainImage + '\n' + 序列化 decls`）。`Make`/`draw` 前若 key 变化则 `invalidateColorSourcePipeline(id)` 并更新 key。  
**不要**每帧无条件 invalidate（避免多余编译）。

- [ ] **Step 4: 测试**

- 扩展 adapter 测试：构造带 `ShaderPaint` 的 `Paint`，经 `TgfxCanvasAdapter::drawPath` 离屏读像素（可复用 ColorSourceEffectTest 的星形/纯色断言，阈值放宽）。
- 手动：App 里绑默认模板 shader，画布应出现 uv 渐变；改 `iTime` 相关源码后播放应动；坏源码不崩、不画。

```bash
cmake --build build --target tgfx_adapter_test
ctest --test-dir build -R 'ColorSource|TgfxRender' --output-on-failure
```

- [ ] **Step 5: 文档 + commit**

更新 `docs/color-source-effect.md` §1.1：预览已接线。  
Spec 状态改为「实现中/已完成」（按实际）。  
勾选 plan → commit：

```bash
git add adapter/tgfx bridge/src/common/motionstudio_bridge_canvas.cpp docs \
  docs/superpowers/plans/2026-08-07-color-source-app-ui.md
git commit -m "Wire ColorSourceEffect into canvas path fills and strokes."
```

---

## Spec 覆盖自检

| Spec 要求 | Task |
|---|---|
| Bridge CRUD / paintMode / serializeShaders | 1 |
| 包 `shader.json` 读写 | 1 |
| ProjectPanel 库 | 2 |
| Sheet：std140 Inputs + mainImage + 模板 | 2 |
| Inspector Color/Shader / 绑定 / uniforms 关键帧 | 3 |
| Paint 快照 + 缺 id 跳过 + 帧上下文 | 4 |
| ColorSourceEffect 接线 + invalidate | 5 |
| 编译失败无特殊 UI | 5（不画即满足） |
| 非目标（高亮/导出/诊断…） | 全 plan 不实现 |

---

## 执行顺序

```
Task 1 → Task 2 ─┐
         Task 3 ─┴→（UI 可写数据）
Task 4 → Task 5   （画布可见过程色）
```

Task 2 与 3 在 Task 1 之后可并行；Task 4/5 可与 2/3 并行，但端到端验收需全部完成。
