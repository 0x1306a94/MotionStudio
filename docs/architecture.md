# 总体架构

Motion Studio 是一个 2D 动效（Motion Graphics）动画制作工具，定位类似 After Effects / Rive 的能力子集：图层 + Transform + 形状 + 关键帧动画。

## 分层架构

```
┌────────────────────────────────────────────────────┐
│  App 层（平台相关）                                  │
│  第一阶段：macOS（Swift + AppKit + Metal）           │
│  职责：UI 交互、渲染目标（CAMetalLayer）、播放控制      │
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
│  MetalRenderAdapter（macOS）/ LottieExporter / 序列帧 │
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
│   ├── model/                      # Document.h Composition.h Layer.h ShapeFill.h ...
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
├── bridge/                         # C++ ↔ Swift 桥接层（extern "C"）
│   ├── include/motionstudio_bridge.h
│   └── src/bridge.cpp
├── app/
│   └── macos/                      # macOS 应用层
│       ├── MotionStudio.xcodeproj
│       ├── Sources/
│       │   ├── AppDelegate.swift
│       │   ├── Document/           # DocumentController.swift
│       │   ├── Timeline/           # TimelineView.swift KeyframeEditor.swift
│       │   ├── Canvas/             # CanvasView.swift（NSView + CAMetalLayer）
│       │   │                       # MetalRenderAdapter.swift
│       │   └── Inspector/          # PropertyInspector.swift
│       └── Resources/Shaders.metal
├── third_party/                    # depctl 按 DEPS 同步，不入库
└── docs/                           # 本目录
```

## 桥接层设计

Swift 与 C++ 核心之间通过 **extern "C" 薄桥接层**通信，而非 Swift 5.9 C++ interop。

**选型理由**：
- Swift C++ interop 对模板类（`Animatable<T>`）和 `unique_ptr` 支持不成熟
- C ABI 边界清晰、调试简单，未来 Web（WASM）/ 移动端壳可直接复用
- 代价是需要手写桥接函数（约 50–80 个），但桥接层保持极薄：只做类型转换和指针传递，不含业务逻辑

接口约定（详见 `bridge/include/motionstudio_bridge.h`）：

```c
// 不透明句柄
typedef void* MSDocumentRef;
typedef void* MSSceneStateRef;

// 文档生命周期
MSDocumentRef ms_document_create(void);
void          ms_document_destroy(MSDocumentRef doc);
MSDocumentRef ms_document_load(const char* json, int jsonLen);
const char*   ms_document_save(MSDocumentRef doc);   // 调用方须 ms_free_string 释放

// Undo / Redo
void ms_document_undo(MSDocumentRef doc);
void ms_document_redo(MSDocumentRef doc);
int  ms_document_can_undo(MSDocumentRef doc);

// 命令执行（UI 的每次编辑对应一个命令）
void ms_command_add_keyframe(MSDocumentRef doc, const char* layerId,
                             const char* propertyPath, int64_t time);
void ms_command_move_keyframe(MSDocumentRef doc, const char* layerId,
                              const char* propertyPath,
                              int64_t oldTime, int64_t newTime);
void ms_command_set_static_value_float(MSDocumentRef doc, const char* layerId,
                                       const char* propertyPath, float value);

// 求值 → 绘制命令
MSSceneStateRef ms_document_evaluate(MSDocumentRef doc, int64_t frameTime);
int             ms_scene_get_command_count(MSSceneStateRef state);
MSDrawCommand   ms_scene_get_command(MSSceneStateRef state, int index);
void            ms_scene_state_destroy(MSSceneStateRef state);

// 变更通知（命令执行/undo 后触发 UI 刷新）
typedef void (*MSDocumentChangedCallback)(void* context);
void ms_document_set_changed_callback(MSDocumentRef doc,
                                      MSDocumentChangedCallback cb, void* context);

// 跨边界内存释放
void ms_free_string(const char* str);
```

**内存约定**：所有由 C ABI 返回的字符串/缓冲区，必须提供对应的 `ms_free_*` 释放函数，由分配方释放。

## 第三方依赖

统一由 [depctl](https://github.com/0x1306a94/depctl) 管理：在根目录 `DEPS` 文件中声明依赖（仓库 URL + 固定 commit），depctl 浅克隆同步到 `third_party/`（不入库），CMake 以 `add_subdirectory` 消费。

| 库 | 用途 | DEPS 声明 |
|---|---|---|
| [GoogleTest](https://github.com/google/googletest) | Core 层单元测试 | `third_party/googletest`（已声明） |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON 序列化/解析 | M1 引入序列化时加入 DEPS（header-only） |

Core 层除此之外零第三方依赖。

同步命令：

```bash
./sync_deps.sh   # 安装构建工具（cmake/ninja 等）与 depctl，再按 DEPS 同步 third_party/
```

## 构建

- 先执行 `./sync_deps.sh` 同步 `third_party/` 依赖
- CMake，`CXX_STANDARD 17`，`POSITION_INDEPENDENT_CODE ON`
- 产物：`libmotionstudio_core.a`（静态库）+ bridge 库
- macOS 应用：Xcode 工程链接上述两个库（或 SwiftPM C target 封装）
- CI：GitHub Actions，macOS runner 上 `sync_deps.sh` → 编译 → ctest

## 相关文档

- 数据模型与 undo/redo：[data-model.md](data-model.md)
- 时间轴与曲线求值：[timeline-evaluation.md](timeline-evaluation.md)
- 渲染抽象与导出：[rendering.md](rendering.md)
- 开发计划：[development-plan.md](development-plan.md)
