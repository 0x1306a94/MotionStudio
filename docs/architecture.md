# 总体架构

Motion Studio 是一个 2D 动效（Motion Graphics）动画制作工具，定位类似 After Effects / Rive 的能力子集：图层 + Transform + 形状 + 关键帧动画。

## 分层架构

```
┌────────────────────────────────────────────────────┐
│  App 层（平台相关）                                  │
│  第一阶段：macOS + iPadOS（SwiftUI + MetalKit）      │
│  职责：UI 交互、渲染目标（MTKView）、播放控制          │
└───────────────────────┬────────────────────────────┘
                        │ extern "C"（bridge/）
┌───────────────────────┴────────────────────────────┐
│  Core 层（纯 C++17，平台无关静态库）                  │
│  职责：动画数据模型、undo/redo、timeline 求值、        │
│       序列化、渲染抽象（输出 DrawCommand）、导出        │
└───────────────────────┬────────────────────────────┘
                        │ RenderAdapter 接口
┌───────────────────────┴────────────────────────────┐
│  适配器（平台相关）                                    │
│  TgfxRenderAdapter（离屏）/ TgfxOnScreenAdapter      │
│  （MTKView 直渲）/ LottieExporter / 序列帧            │
└────────────────────────────────────────────────────┘
```

**核心原则**：Core 层不知道任何渲染后端的存在。它在指定时间对场景求值，输出扁平化的绘制命令（`DrawCommandList`），由适配器消费。这样同一份动画数据可以对接 Metal 实时预览、Lottie 导出、未来的 OpenGL/Vulkan 渲染器。

## 模块划分

模块即 `src/`、`include/MotionStudio/`、`tests/` 下的同名子目录（`core/` 存放版本等库级基础设施）。

| 模块 | 职责 |
|---|---|
| `common/` | 基础类型：`EntityId`、`FrameTime`/`FrameRate`、`Vec2`/`Mat3`/`Color`、`BezierPath` |
| `model/` | 动画数据模型：`Document`、`Composition`、`Layer`、`Shape`、`Transform`、`PropertyPath` |
| `animation/` | `Animatable<T>`、`Keyframe`、`Easing`、贝塞尔缓动求值、插值策略 |
| `undo/` | `Command` 接口、`UndoManager`、内置命令集 |
| `render/` | `SceneEvaluator`、`SceneState`、`DrawCommand`、`RenderAdapter` 接口 |
| `export/` | `LottieExporter`、序列帧导出 |
| `serialization/` | `Serializer`、DTO（与运行时模型解耦的文件格式）、`SchemaMigrator` |

模块依赖方向（单向，禁止反向依赖）：

```
common ← model ← animation ← undo
                ↑
        render（依赖 model + animation）
        export（依赖 model + render）
        serialization（依赖 model）
```

## 目录结构

```
motionstudio/
├── CMakeLists.txt                  # 顶层 CMake
├── include/MotionStudio/           # 公共头文件（按模块分子目录，一类型一文件，
│   │                               # 非模板实现一律放 src/，不暴露第三方类型）
│   ├── core/                       # Version.h
│   ├── common/                     # EntityId.h Time.h Vec2.h Color.h Mat3.h
│   │                               # BezierPath.h Expected.h
│   ├── model/                      # Document.h Composition.h Layer.h ShapePath.h ...
│   ├── animation/                  # Animatable.h Keyframe.h Easing.h Interpolator.h
│   ├── undo/                       # Command.h UndoManager.h AddKeyframeCommand.h ...
│   ├── render/                     # RenderAdapter.h SceneEvaluator.h DrawCommand.h ...
│   ├── export/                     # LottieExporter.h
│   └── serialization/              # Serializer.h SchemaMigrator.h Dto.h
├── src/                            # 实现文件（与 include/ 同构，按模块分子目录）
│   ├── CMakeLists.txt              # core 静态库（libmotionstudio_core.a）
│   ├── core/ common/ model/ animation/ undo/ render/ export/ serialization/
├── tests/                          # GoogleTest 单元测试（与 src/ 同构）
│   ├── CMakeLists.txt              # core_tests + gtest_discover_tests
│   └── core/ ...
├── adapter/tgfx/                   # tgfx（Metal 后端）渲染适配器
│   ├── TgfxCanvasAdapter.mm        # 公共基类（tgfx 画布操作 + 转换）
│   ├── TgfxRenderAdapter.mm        # 离屏（快照测试 / 序列帧导出）
│   └── TgfxOnScreenAdapter.mm      # MTKView 直渲（编辑器画布）
├── bridge/                         # C++ ↔ Swift 桥接层（extern "C"）
│   ├── include/motionstudio_bridge.h
│   ├── src/motionstudio_bridge.cpp          # 文档/undo/查询/命令（跨平台）
│   └── src/motionstudio_bridge_canvas.mm    # 画布 API（仅 Apple）
├── apps/
│   ├── gen_mac                     # 生成 gen_xcode（CMake Xcode 工程）的脚本
│   ├── gen_xcode/                  # CMake 生成的 Xcode 工程，产物在 Products/
│   ├── MotionStudio.xcworkspace    # 组合 gen_xcode + MotionStudioApp
│   └── MotionStudioApp/            # macOS + iPadOS 应用层（SwiftUI）
│       ├── Configurations/         # Base/Developer.xcconfig（搜索路径、链接）
│       └── MotionStudioApp/
│           ├── Bridge/             # Swift 桥接头（导入 motionstudio_bridge.h）
│           ├── Document/           # MotionDocument（ReferenceFileDocument）
│           ├── Model/              # MotionDocumentCore / EditorState
│           ├── Canvas/             # CanvasView（MTKView + CADisplayLink 播放）
│           ├── Timeline/           # TimelineView（左图层栈 + 右关键帧轨道，垂直滚动）
│           ├── Inspector/          # InspectorView（Transform 属性编辑）
│           ├── ProjectPanel/       # ProjectPanelView（素材 / 合成 / 建层工具栏）
│           └── EditorRootView.swift
├── third_party/                    # depctl 按 DEPS 同步，不入库
└── docs/                           # 本目录
```

应用构建依赖 `gen_xcode/Products/$(CONFIGURATION)` 下的静态库（core / bridge / tgfx 适配器）与 `gen_xcode/tgfx_prebuilt/` 下的 tgfx 预编译库（源码来自 `third_party/libpag/third_party/tgfx`）；PAG 导出链 `adapter/pag_codec`（只编译 libpag 的 base+codec）。搜索路径与链接标志在 `Base.xcconfig` 中按 SDK 配置。

## 桥接层设计

Swift 与 C++ 核心之间通过 **extern "C" 薄桥接层**通信，而非 Swift 5.9 C++ interop。

**选型理由**：
- Swift C++ interop 对模板类（`Animatable<T>`）和 `unique_ptr` 支持不成熟
- C ABI 边界清晰、调试简单，未来 Web（WASM）/ 移动端壳可直接复用
- 代价是需要手写桥接函数（约 50–80 个），但桥接层保持极薄：只做类型转换和指针传递，不含业务逻辑

接口约定（完整定义见 `bridge/include/motionstudio_bridge.h`）：

```c
// 不透明句柄（Swift 导入为 OpaquePointer）
typedef struct MSDocument MSDocument;
typedef struct MSCanvas MSCanvas;

// 文档生命周期
MSDocument *ms_document_create(void);   // 含一个默认合成
MSDocument *ms_document_load(const char *jsonText, size_t length, char **errorOut);
void        ms_document_destroy(MSDocument *document);
char       *ms_document_save(MSDocument *document);  // 调用方须 ms_string_free 释放
void        ms_string_free(char *string);

// Undo / Redo
bool ms_document_undo(MSDocument *document);
bool ms_document_redo(MSDocument *document);
bool ms_document_can_undo(MSDocument *document);
void ms_document_end_merge_group(MSDocument *document);  // 拖拽结束时关闭合并窗口

// 查询（合成 / 图层 / 属性 / 关键帧，全部 null-safe）
int      ms_composition_layer_count(MSDocument *document, uint64_t compositionId);
uint64_t ms_layer_id_at(MSDocument *document, uint64_t compositionId, int index);
int      ms_property_keyframe_count(MSDocument *document, uint64_t entityId, const char *path);
float    ms_property_evaluate_float(MSDocument *document, uint64_t entityId,
                                    const char *path, int64_t frame);

// 命令执行（UI 的每次编辑对应一个命令，全部可撤销）
void ms_command_set_static_float(MSDocument *document, uint64_t entityId,
                                 const char *path, float value);
void ms_command_add_keyframe_float(MSDocument *document, uint64_t entityId,
                                   const char *path, int64_t frame, float value);
void ms_command_move_keyframe(MSDocument *document, uint64_t entityId, const char *path,
                              int64_t oldFrame, int64_t newFrame);
uint64_t ms_command_add_rect_layer(MSDocument *document, uint64_t compositionId);

// 画布（仅 Apple 平台）：内部完成 evaluate → BuildCommands → PlayCommands，
// DrawCommand 不越过 C ABI 边界
MSCanvas *ms_canvas_create(void *mtkView);
void      ms_canvas_destroy(MSCanvas *canvas);
void      ms_canvas_draw_frame(MSCanvas *canvas, MSDocument *document,
                               uint64_t compositionId, int64_t frame);
```

**内存约定**：所有由 C ABI 返回的字符串，一律经 `ms_string_free` 释放。

**变更通知**：无回调机制——Swift 侧 `MotionDocumentCore` 在每次命令后自增 `revision`（`@Observable`），视图读取它来订阅模型变化。

## 第三方依赖

统一由 [depctl](https://github.com/0x1306a94/depctl) 管理：在根目录 `DEPS` 文件中声明依赖（仓库 URL + 固定 commit），depctl 浅克隆同步到 `third_party/`（不入库）。

| 库 | 用途 | 工作区路径 | 如何进构建 |
|---|---|---|---|
| [GoogleTest](https://github.com/google/googletest) | 单元测试 | `third_party/googletest` | `add_subdirectory` |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON 序列化 | `third_party/json` | `add_subdirectory` |
| [libpag](https://github.com/Tencent/libpag) | PAG 编解码源码 + 内嵌 tgfx | `third_party/libpag` | **不**整库 `add_subdirectory`；见下 |
| [tgfx](https://github.com/libpag/tgfx) | 2D 渲染（Metal） | `third_party/libpag/third_party/tgfx` | MS 预编译 `tgfx.a`（`cmake/BuildTgfx.cmake`） |

Core 库本身仅依赖 nlohmann/json（私有链接，不经公共头暴露）。

### Apple：tgfx + pag_codec（为何分叉构建）

预览需要 **Metal** tgfx；上游整库 libpag 偏 **GL/平台渲染**，且 **Mac Catalyst 无 OpenGLES**。导出只需要 `Codec::Encode` / `File::Load`，不必链 libpag 播放器。因此：

1. **一份源码**：DEPS 只 pin `libpag`；tgfx 随其 `third_party/tgfx`；**无**独立 `third_party/tgfx`。
2. **Metal 预编译**：根 CMake 设 `TGFX_*` / `LIBPAG_DIR` → `adapter/tgfx` 用 `BuildTgfx.cmake` 产出  
   `tgfx_prebuilt/<Config>/<mac|ios|catalyst>/<arch>/tgfx.a`，adapter / bridge / App 链接该库。
3. **编解码裁剪**：`adapter/pag_codec` 只编译 libpag 的 `src/base` + `src/codec`，加 `PlatformStub`（不拉 NativePlatform / 视频解码），并链**同一份**预编译 tgfx。
4. **Catalyst patch**：`patches/libpag-tgfx-maccatalyst-arm64.patch` 与 `libpag-tgfx-vendor_tools-maccatalyst-arm64.patch`（由 `DEPS` actions apply）。

```
DEPS → third_party/libpag (+ …/tgfx)
         │
         ├─ BuildTgfx (Metal) ──► tgfx.a ──► tgfx_adapter / App / bridge / pag_codec
         └─ adapter/pag_codec (base+codec) ──► 后续 pag_export PRIVATE 链接
```

细节、否决方案与验收清单：[PAG Export 设计 §0](superpowers/specs/2026-07-31-pag-export-design.md)。

同步命令：

```bash
./sync_deps.sh   # 安装构建工具（cmake/ninja 等）与 depctl，再按 DEPS 同步 third_party/
```

## 构建

**Core / bridge / 适配器**（CMake，`CXX_STANDARD 17`，`POSITION_INDEPENDENT_CODE ON`）：

```bash
./sync_deps.sh                          # 同步 third_party/
cmake -B build -G Ninja && cmake --build build && ctest --test-dir build
```

**应用层**：

```bash
apps/gen_mac                            # 生成 apps/gen_xcode（CMake Xcode 工程）
xcodebuild -project apps/gen_xcode/MotionStudio.xcodeproj -target bridge \
    -configuration Debug -sdk macosx    # 构建静态库到 gen_xcode/Products/
# 之后用 apps/MotionStudio.xcworkspace 构建 / 运行 MotionStudioApp
```

- CI：GitHub Actions，macOS runner 上 `sync_deps.sh` → 编译 → ctest

## 相关文档

- 数据模型与 undo/redo：[data-model.md](data-model.md)
- 时间轴与曲线求值：[timeline-evaluation.md](timeline-evaluation.md)
- 渲染抽象与导出：[rendering.md](rendering.md)
- PAG 导出与依赖收敛：[superpowers/specs/2026-07-31-pag-export-design.md](superpowers/specs/2026-07-31-pag-export-design.md)
- 开发计划：[development-plan.md](development-plan.md)
