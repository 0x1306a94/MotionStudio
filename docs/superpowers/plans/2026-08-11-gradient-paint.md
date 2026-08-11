# Gradient Paint — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 Fill/Stroke 增加 Linear/Radial/Conic/Diamond 渐变（`StylePaintMode::Gradient`），支持几何与色标关键帧、Inspector 与画布手柄编辑，PAG 导出 Linear/Radial；Conic/Diamond/Shader 跳过并由用户自选 BMP。

**Architecture:** kind（`paintMode`）只选择当前求值路径，`color` / `gradient` / `shader*` 三态共存且切换不清空；Core 求值出 `EvaluatedGradient` 快照，adapter 调 tgfx `Make*Gradient`；非法当前 kind 静默跳过该 paint。画布手柄仿 PathEdit chrome。

**Tech Stack:** C++17 Core、nlohmann/json、GoogleTest、bridge C ABI、tgfx adapter、SwiftUI App、`pag_export`。

**Spec:** `docs/superpowers/specs/2026-08-11-gradient-paint-design.md`

## Global Constraints

- 分支：非 `master` 时直接在当前分支提交；若在 `master` 先按 `feature/{username}_gradient_paint` 建分支。
- **自动 commit：** 每完成一个 Task 必须提交；**提交前先**把本 plan 对应 checkbox 改为 `[x]`、更新 `**Status:**`，再把代码与本 plan 一并 commit。
- Commit 信息：英语、≤120 字符、句号结尾、句中无其他标点；侧重用户可感知变化。
- 每完成一个 Step 立刻勾选；未同步 plan 视为该步未完成。
- Core / bridge / adapter / pag_export 宣称完成前优先 ASan：
  `cmake -B build -G Ninja -DMOTIONSTUDIO_ENABLE_ASAN=ON && cmake --build build`
- App / Xcode：优先 Xcode MCP `BuildProject`；不可用再 `xcodebuild`（见 `AGENTS.md`）。
- 编码规范：禁异常、禁 `dynamic_cast`、错误用 `Expected`、禁 lambda 优先显式函数（测试与现有 SceneEvaluator 局部 lambda 除外，跟文件既有风格）。
- Core **不**链接 tgfx；渐变 GPU 创建仅在 `adapter/tgfx`。
- **不做：** Lottie 渐变、PAG Angle/Reflected/Diamond、stop midpoint、画布拖 stop、渐变库资源化。

---

## 文件对照

| 文件 | 职责 |
|---|---|
| `include/MotionStudio/model/GradientType.h` | `GradientType` 枚举 |
| `include/MotionStudio/model/GradientPaint.h` | `GradientStop` / `GradientPaint` 模型 |
| `include/MotionStudio/model/StylePaintMode.h` | 增加 `Gradient = 2` |
| `include/MotionStudio/model/LayerStyle.h` | Fill/Stroke 增加 `gradient` |
| `include/MotionStudio/model/LayerStylePaint.h` + `.cpp` | 懒初始化默认渐变；kind 切换辅助（**不再** Clear 清字段） |
| `include/MotionStudio/model/PropertyPath.h` + `src/model/PropertyPath.cpp` | `styles[i].gradient.*` |
| `include/MotionStudio/undo/CommandKind.h` | `SetGradientType` / `AddGradientStop` / `RemoveGradientStop` |
| `include/MotionStudio/undo/SetStylePaintModeCommand.h` + `.cpp` | 只改 kind + 懒初始化 |
| `include/MotionStudio/undo/SetGradientTypeCommand.h` + `.cpp` | 改 type |
| `include/MotionStudio/undo/AddGradientStopCommand.h` + `.cpp` | 增 stop |
| `include/MotionStudio/undo/RemoveGradientStopCommand.h` + `.cpp` | 删 stop（N≥2） |
| `src/serialization/Dto.cpp` + `Serializer.cpp` | paintMode/gradient JSON |
| `include/MotionStudio/render/Paint.h` | `EvaluatedGradient` |
| `src/render/SceneEvaluator.cpp` | 按 kind 求值；非法跳过 |
| `adapter/tgfx/src/TgfxCanvasAdapter.cpp` | `Make*Gradient` |
| `bridge/include/motionstudio_bridge.h` + shader/style bridge | `MS_PAINT_MODE_GRADIENT` + gradient API |
| `apps/.../StyleShaderPaintControls.swift` 等 | Inspector 三段 + Gradient 面板 |
| `include/MotionStudio/render/GradientEditHandles.h` + `.cpp` | 画布手柄 build/hit/draw |
| `apps/.../CanvasViewController.swift` | 手柄交互 |
| `src/export/pag/PagFileBuilder.cpp` | Linear/Radial → GradientFill/Stroke；其余跳过 + warning |
| `docs/data-model.md` / spec 状态 | 文档同步 |
| 测试 | `tests/model/*`、`tests/undo/*`、`tests/serialization/*`、`tests/render/*`、`adapter/tgfx/tests/*`、`tests/export/pag/*`、`bridge/tests/*` |

---

### Task 1: 模型类型 `GradientType` / `GradientPaint` + `StylePaintMode::Gradient`

**Status:** ✅ Done

**Files:**
- Create: `include/MotionStudio/model/GradientType.h`
- Create: `include/MotionStudio/model/GradientPaint.h`
- Modify: `include/MotionStudio/model/StylePaintMode.h`
- Modify: `include/MotionStudio/model/LayerStyle.h`
- Create: `tests/model/GradientPaintTest.cpp`
- Modify: CMake/tests 注册（若需显式加源；glob 则自动）

**Interfaces:**
- Produces:
```cpp
enum class GradientType : uint8_t { Linear = 0, Radial = 1, Conic = 2, Diamond = 3 };
struct GradientStop {
    Animatable<Color> color{Color{0, 0, 0, 1}};
    Animatable<float> position{0.f};
};
struct GradientPaint {
    GradientType type = GradientType::Linear;
    Animatable<Vec2> start{Vec2{0, 0}};
    Animatable<Vec2> end{Vec2{100, 0}};
    Animatable<float> startAngle{0.f};
    Animatable<float> endAngle{360.f};
    std::vector<GradientStop> stops;
};
// StylePaintMode::Gradient = 2
// FillStyle / StrokeStyle 增加：GradientPaint gradient;
```

- [x] **Step 1: 写失败测试**

```cpp
TEST(GradientPaintTest, DefaultsAndStyleHoldsGradient) {
    FillStyle fill;
    EXPECT_EQ(fill.paintMode, StylePaintMode::Color);
    EXPECT_EQ(fill.gradient.type, GradientType::Linear);
    EXPECT_TRUE(fill.gradient.stops.empty());
    fill.paintMode = StylePaintMode::Gradient;
    fill.gradient.stops.push_back({});
    fill.gradient.stops[0].color.setStaticValue(Color{1, 0, 0, 1});
    fill.gradient.stops[0].position.setStaticValue(0.f);
    EXPECT_EQ(fill.paintMode, StylePaintMode::Gradient);
    EXPECT_EQ(fill.gradient.stops.size(), 1u);
}
```

- [x] **Step 2: 跑测试确认失败**

Run: `cmake --build build && ./build/tests/core_tests --gtest_filter='GradientPaintTest.*'`  
Expected: 编译失败（缺类型）或链接失败

- [x] **Step 3: 最小实现**

按 Interfaces 增加头文件与 `LayerStyle` 成员；更新 `StylePaintMode` 注释为「kind 选择 Color/Shader/Gradient，三态数据共存」。

- [x] **Step 4: 跑测试确认通过**

Run: 同上  
Expected: PASS

- [x] **Step 5: 勾选本 Task、更新 Status、commit**

```bash
git commit --only include/MotionStudio/model/GradientType.h \
  include/MotionStudio/model/GradientPaint.h \
  include/MotionStudio/model/StylePaintMode.h \
  include/MotionStudio/model/LayerStyle.h \
  tests/model/GradientPaintTest.cpp \
  docs/superpowers/plans/2026-08-11-gradient-paint.md \
  -m "Add gradient paint model types on fill and stroke."
```

---

### Task 2: kind 切换不清空 + 懒初始化

**Status:** ✅ Done

**Files:**
- Modify: `include/MotionStudio/model/LayerStylePaint.h` + `src/model/LayerStylePaint.cpp`
- Modify: `include/MotionStudio/undo/SetStylePaintModeCommand.h` + `src/undo/SetStylePaintModeCommand.cpp`
- Modify: `tests/model/LayerStylePaintModeTest.cpp`
- Modify: `tests/undo/ShaderCommandTest.cpp`（断言改为「切回 Color 不丢 shaderId」）

**Interfaces:**
- Produces:
```cpp
// LayerStylePaint.h
bool GradientStopsAreValid(const GradientPaint &gradient);  // size≥2, pos 0..1 严格递增
void EnsureDefaultGradient(GradientPaint &gradient, Vec2 start, Vec2 end);  // 仅 stops<2 时填充
// SetStylePaintModeCommand::execute:
//   - 只改 paintMode
//   - Gradient 且 stops<2 → EnsureDefaultGradient（bounds 由 command 调用方传入或内部算 AABB）
//   - Shader 且 shaderId 无效 → BindShaderPaint(first)（库空则 no-op / 不改 mode）
//   - 切到 Color：**不**调用 ClearShaderPaint
// ClearShaderPaint / BindShaderPaint 保留给显式解绑/绑定，kind 切换不再 Clear
```

- [x] **Step 1: 写失败测试**
- [x] **Step 2: 跑测试确认失败**（旧 Clear 行为会失败）
- [x] **Step 3: 改 `SetStylePaintModeCommand` + `EnsureDefaultGradient`**
- [x] **Step 4: 跑测试确认通过**
- [x] **Step 5: 勾选、commit**
---

### Task 3: PropertyPath 解析 `styles[i].gradient.*`

**Status:** ✅ Done

**Files:**
- Modify: `include/MotionStudio/model/PropertyPath.h`（注释示例）
- Modify: `src/model/PropertyPath.cpp`（`resolveStyleProperty` / 新增 gradient 段解析）
- Modify: `tests/model/PropertyPathTest.cpp`

**Interfaces:**
- Produces 可解析路径：
  - `styles[0].gradient.start` → `Animatable<Vec2>*`
  - `styles[0].gradient.end`
  - `styles[0].gradient.startAngle` / `endAngle` → `Animatable<float>*`
  - `styles[0].gradient.stops[0].color` → `Animatable<Color>*`
  - `styles[0].gradient.stops[0].position` → `Animatable<float>*`
- 越界 stop index → `nullptr`

- [x] **Step 1: 写失败测试**（解析 + setStatic/evaluate round-trip）
- [x] **Step 2: 跑测失败**
- [x] **Step 3: 实现解析**（在 `resolveStyleProperty` 旁增加 `resolveGradientProperty(Fill/Stroke, segments)`；禁止 lambda，用显式函数）
- [x] **Step 4: 跑测通过**
- [x] **Step 5: 勾选、commit**  
  Message: `Resolve animatable paths for gradient paint properties.`

---

### Task 4: 序列化 round-trip

**Status:** ✅ Done

**Files:**
- Modify: `src/serialization/Dto.cpp`（`ToString` / `stylePaintModeFromString` / gradient type 字符串）
- Modify: `src/serialization/Serializer.cpp`（`AppendStylePaintToJson` / `StylePaintFromJson` 写读 `gradient`；Color 仍可省略 paintMode；Gradient/Shader 写 `paintMode`）
- Modify: `tests/serialization/SerializerTest.cpp`

**Interfaces:**
- JSON：`paintMode: "gradient"` + `gradient: { type, start, end, startAngle, endAngle, stops:[{color,position}] }`
- 缺 `paintMode` → Color（schema 1 兼容）
- 未知 paintMode → `Unexpected`
- Gradient 反序列化后若 stops 非法 → `Unexpected`（或加载后校验阶段失败，与现有 Validate 风格一致）

- [x] **Step 1: 写失败测试**（四种 type + 关键帧几何/stop 往返；切 kind 后三态字段都在）
- [x] **Step 2–4: 实现并跑通**
- [x] **Step 5: commit**  
  Message: `Serialize and load gradient paint on fill and stroke.`

---

### Task 5: `SetGradientType` / Add/Remove stop 命令

**Status:** ✅ Done

**Files:**
- Modify: `include/MotionStudio/undo/CommandKind.h`
- Create: `SetGradientTypeCommand` / `AddGradientStopCommand` / `RemoveGradientStopCommand`（h+cpp）
- Create: `tests/undo/GradientCommandTest.cpp`
- Modify: undo 源文件 CMake（若需）

**Interfaces:**
```cpp
class SetGradientTypeCommand : public Command {
  // layerId, styleIndex, GradientType
};
class AddGradientStopCommand : public Command {
  // layerId, styleIndex, insertIndex, Color color, float position
  // 插入后必要时夹紧/排序由实现约定：保持首 0 末 1；中间严格递增（冲突则微调 position）
};
class RemoveGradientStopCommand : public Command {
  // layerId, styleIndex, stopIndex
  // stops.size()<=2 时 execute no-op
};
```

- [x] **Step 1–4: TDD 覆盖 type 切换、增删、undo/redo、N≥2**
- [x] **Step 5: commit**  
  Message: `Add undo commands for gradient type and color stops.`

---

### Task 6: SceneEvaluator 按 kind 求值；非法跳过

**Status:** 未开始

**Files:**
- Modify: `include/MotionStudio/render/Paint.h`（`EvaluatedGradient` / `EvaluatedGradientStop`）
- Modify: `src/render/SceneEvaluator.cpp`（`ApplyLayerStyles` 三分支；去掉「非 Shader 即 Color」）
- Create 或 Modify: `tests/render/SceneEvaluatorGradientPaintTest.cpp`

**Interfaces:**
```cpp
struct EvaluatedGradientStop { Color color; float position; };
struct EvaluatedGradient {
    GradientType type = GradientType::Linear;
    Vec2 start, end;
    float startAngle = 0.f, endAngle = 360.f;
    std::vector<EvaluatedGradientStop> stops;
};
// Paint 增加 EvaluatedGradient gradient;
bool MakeGradientPaint(const GradientPaint &src, PreviewTime time, EvaluatedGradient &out);
// 合法才写 out 并 return true；否则 false → ApplyLayerStyles 直接 return（不 push item）
```

合法条件（spec §2.1.1）：stops≥2；首 pos=0 末=1 中间严格递增；Radial/Diamond 还要求 `Distance(start,end) > 0`。

- [ ] **Step 1: 写失败测试**
  - 合法 Linear → item.paint.paintMode==Gradient 且 stops 求值正确  
  - `stops.size()<2` → **不产生** EvaluatedShapeItem  
  - Shader 失败仍跳过（回归）  
  - kind=Gradient 时即使 color/shader 有值也**不**用它们

- [ ] **Step 2–4: 实现并跑通**

- [ ] **Step 5: commit**  
  Message: `Evaluate gradient paints and skip invalid style draws.`

---

### Task 7: tgfx adapter `Make*Gradient`

**Status:** 未开始

**Files:**
- Modify: `adapter/tgfx/src/TgfxCanvasAdapter.cpp`（扩展 `MakePaintImageShader` 或并列 `MakePaintShader`：Color→nullopt 走 solid；Shader→ColorSource；Gradient→tgfx gradient；非法→empty skip）
- Modify: `adapter/tgfx/tests/TgfxRenderAdapterTest.cpp`

**Interfaces:**
```cpp
// Gradient 分支（伪代码）：
// colors/positions 从 paint.gradient.stops 填充
// Linear → MakeLinearGradient
// Radial → MakeRadialGradient(start, Distance(start,end), ...)
// Conic → MakeConicGradient(start, startAngle, endAngle, ...)
// Diamond → MakeDiamondGradient(start, Distance(start,end), ...)
// radius<=0 或 stops 空 → 返回 empty shared_ptr（跳过 draw）
```

- [ ] **Step 1: 写失败/基线测试**（四种 type 各画一帧不崩；可对标现有 `ShaderFillDrawsUvGradient` 用离屏像素非全透明断言）

- [ ] **Step 2–4: 实现并跑通**  
  Run: `./build/adapter/tgfx/tests/tgfx_adapter_test --gtest_filter='*Gradient*'`

- [ ] **Step 5: commit**  
  Message: `Draw linear radial conic and diamond gradients in tgfx.`

---

### Task 8: Bridge + Swift `MS_PAINT_MODE_GRADIENT`

**Status:** 未开始

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`（`MS_PAINT_MODE_GRADIENT = 2`；gradient 读 API；set type / add/remove stop）
- Modify: `bridge/src/common/motionstudio_bridge_shader.cpp`（或新建 `motionstudio_bridge_gradient.cpp`）
- Modify: `bridge/tests/BridgeShaderTest.cpp` 或 Create `BridgeGradientTest.cpp`
- Modify: `apps/.../Bridge/MotionStudioBridgingExtension.swift`（`allCases` + label）
- Modify: `apps/.../Model/MotionDocumentCore.swift`
- Modify: `apps/.../Bridge/PropertyPath.swift`（gradient path 辅助）
- 遵守 `.claude/rules/bridge-swift-enums.md`

**Interfaces（C ABI 示意）：**
```c
MS_PAINT_MODE_GRADIENT = 2,
typedef CF_CLOSED_ENUM(int, MS_GRADIENT_TYPE) { ... };

MS_GRADIENT_TYPE ms_layer_style_gradient_type_at(...);
int ms_layer_style_gradient_stop_count(...);
// 几何/角度走现有 ms_evaluate_* / setStatic / addKeyframe + path
bool ms_document_set_gradient_type(...);
bool ms_document_add_gradient_stop(..., int index, float r,g,b,a, float position);
bool ms_document_remove_gradient_stop(..., int stopIndex);
// set_style_paint_mode: mode=GRADIENT 时 shaderId 可传 0
```

- [ ] **Step 1–4: Bridge 测试 + Swift 编译**（Xcode MCP 或 xcodebuild）

- [ ] **Step 5: commit**  
  Message: `Expose gradient paint mode and stops through the bridge.`

---

### Task 9: Inspector Gradient 面板

**Status:** 未开始

**Files:**
- Modify: `apps/.../Inspector/StyleShaderPaintControls.swift`（三段 Picker；Gradient 子面板：type、start/end、angles、stops +/-）
- Modify: `apps/.../Inspector/FillsInspector.swift` / `StrokesInspector.swift`（ColorPicker **仅** Color kind）
- Modify: `apps/.../Timeline/Root/TimelineSupport.swift`（Gradient kind 下列 gradient 关键帧轨；非当前 kind 不列）

**Interfaces:**
- UI 路径用 `StyleProperty.gradientStart(styleIndex:)` 等（Task 8 已加）
- 改 type → `setGradientType`；增删 stop → bridge 命令
- 关键帧控件复用 `NumberPropertyRow` / ColorPicker + diamond，与 shader uniform 行同模式

- [ ] **Step 1: 实现 UI（无单独 gtest；以 Xcode 编译 + 手动点选为准）**

- [ ] **Step 2: Xcode 编译 MotionStudioApp（Catalyst 或当前 destination）**

- [ ] **Step 3: 勾选、commit**  
  Message: `Add gradient controls to the fill and stroke inspector.`

---

### Task 10: 画布渐变手柄

**Status:** 未开始

**Files:**
- Create: `include/MotionStudio/render/GradientEditHandles.h`
- Create: `src/render/GradientEditHandles.cpp`
- Create: `tests/render/GradientEditHandlesTest.cpp`
- Modify: bridge canvas API（hit / 可选 build commands）
- Modify: `apps/.../Canvas/CanvasViewController.swift`（选中层 + 第一个 Gradient style：显示 chrome；命中优先于 free-transform；drag 写 path + merge group）

**Interfaces:**
```cpp
enum class GradientHandleKind { None, Start, End, StartAngle, EndAngle };
struct GradientEditTarget { EntityId layerId; int styleIndex; };
struct GradientEditHandles {
    bool valid = false;
    GradientEditTarget target;
    GradientType type = GradientType::Linear;
    Vec2 worldStart{}, worldEnd{};
    float startAngle = 0.f, endAngle = 360.f;
    Mat3 worldTransform = Mat3::Identity();
};
bool BuildGradientEditHandles(const SceneState&, GradientEditTarget, GradientEditHandles&);
GradientHandleKind HitTestGradientEdit(const GradientEditHandles&, Vec2 scenePoint, float hitRadius);
DrawCommandList BuildGradientEditCommands(const GradientEditHandles&, float strokeWidth, float handleSize);
```

- Linear：Start/End + 线  
- Radial/Diamond：Start=center、End=radiusPoint + 示意  
- Conic：Center + 两角射线（拖角度写 `startAngle`/`endAngle`）

- [ ] **Step 1–4: Core hit 测试 TDD → Bridge → Swift 接线 → 编译**

- [ ] **Step 5: commit**  
  Message: `Add canvas handles for editing gradient geometry.`

---

### Task 11: PAG 导出 Linear/Radial + 跳过提示

**Status:** 未开始

**Files:**
- Modify: `src/export/pag/PagFileBuilder.cpp`（`appendFill` / stroke：按 kind 分支）
- Modify: `tests/export/pag/PagExporterTest.cpp`
- Modify: App 导出 UI（若已有 warning 展示：追加「Conic/Diamond/Shader 未导出，可用 BMP」；无则只保证 `PagExportWarning` 可被 Bridge/UI 读到）

**行为：**
| kind | 动作 |
|---|---|
| Color | 现有 FillElement/StrokeElement |
| Gradient Linear/Radial | `GradientFillElement` / `GradientStrokeElement`：`fillType`、`startPoint`/`endPoint`、`colors`（从 stops 打包）；opacity 按现有惯例 |
| Gradient Conic/Diamond | **不** append 该 paint；`warnings_` 加一条（含 layerId/名，提示可用 BMP） |
| Shader | 同现有：跳过或已有策略；保证不崩，warning 提示 BMP |

- [ ] **Step 1: 写失败测试**（Linear fill 导出后 File 含 GradientFill；Conic 无对应元素但 Export 成功 + warning）

- [ ] **Step 2–4: 实现并跑通**  
  Run: `./build/tests/export/pag/...` 或 ctest `-R PagExporter`

- [ ] **Step 5: commit**  
  Message: `Export linear and radial gradients to PAG files.`

---

### Task 12: 文档同步

**Status:** 未开始

**Files:**
- Modify: `docs/data-model.md`（StylePaintMode 三态共存、GradientPaint、PropertyPath）
- Modify: `docs/rendering.md`（若有 Paint 小节）
- Modify: `docs/superpowers/specs/2026-08-11-gradient-paint-design.md` 状态 → 实现中/完成（随进度）

- [ ] **Step 1: 按已实现行为改文档（勿写未做手柄/导出若仍未合）**

- [ ] **Step 2: commit**  
  Message: `Document gradient paint mode in the data model.`

---

## Spec 覆盖自检

| Spec 要求 | Task |
|---|---|
| StylePaintMode::Gradient + 模型 | 1 |
| kind 切换不清空 + 懒初始化 | 2 |
| N stops 可动画 PropertyPath | 3、5 |
| 序列化 | 4 |
| 只求值当前 kind；非法跳过 | 6 |
| tgfx 四种渐变 | 7 |
| Inspector 三段 + 面板 | 8、9 |
| 画布手柄 | 10 |
| PAG Linear/Radial；Conic/Diamond 跳过 + BMP 提示 | 11 |
| 文档 | 12 |

## 执行手势

实现时推荐 **subagent-driven-development**（每 Task 新子代理 + 复审），或本会话 **executing-plans** 连续做。
