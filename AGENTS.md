# AGENTS.md

Motion Studio 是一个 2D 动效（Motion Graphics）动画工具（类似 After Effects / Rive 的子集）：图层 + Transform + 形状 + 关键帧动画。核心是平台无关的 C++17 静态库，第一阶段应用层为 macOS Catalyst + iPadOS（SwiftUI + MetalKit）。

## 常用命令

第三方依赖由 depctl 按根目录 `DEPS` 文件同步到 `third_party/`（不入库），任何构建前先同步：

```bash
# 安装 cmake/ninja 等工具 + depctl，再同步 third_party/
./sync_deps.sh
```

构建与测试（Ninja，提交前必须带 ASan）：

```bash
# 同时开启 ASan + UBSan
cmake -B build -G Ninja -DMOTIONSTUDIO_ENABLE_ASAN=ON
cmake --build build
# 全部测试
ctest --test-dir build --output-on-failure
```

测试分三个二进制（均经 `gtest_discover_tests` 注册）：`core_tests`、`bridge_test`、`tgfx_adapter_test`（Apple 平台）。运行单个测试：

```bash
# gtest 过滤（更细粒度）
./build/tests/core_tests --gtest_filter='AnimatableTest.*'
# 或 ctest 正则
ctest --test-dir build -R 'SerializerTest' --output-on-failure
```

性能基准（CI 仅记录不阻断，带 `benchmark` 标签；注意普通 ctest 也会跑它，用 `-LE benchmark` 排除）：

```bash
ctest --test-dir build -L benchmark
```

代码格式化（clang-format 14，经 pipx 安装；Swift 用 swiftformat）：

- **提交时**：由 git hook `pre-commit` 自动格式化本次暂存的 C++/ObjC/Swift 源码（`git_hooks/pre-commit`，经 `./sync_deps.sh` 安装到 `.git/hooks/pre-commit`）。日常提交无需再手动跑全量脚本。
- **全量格式化**：需要时手动执行 `./codeformat.sh`（扫描约定源码目录并 in-place 格式化；仅用于手动全量整理，不是提交流程门禁）。

应用层（Apple 平台）：

```bash
# 用 CMake 生成 apps/gen_xcode Xcode 工程，产物在 gen_xcode/Products/
apps/gen_mac

# 之后用 MotionStudio.xcworkspace 构建 / 运行 MotionStudioApp
```

编译 Xcode 工程时**优先使用 Xcode MCP**，不可用再回退 `xcodebuild`：

1. **检查 MCP 是否可用**：探测 `user-xcode`（或等价 Xcode MCP）是否 `ready`；可用则先调 `XcodeListWindows` 确认已打开 `MotionStudio.xcworkspace` 并拿到 `tabIdentifier`。若 server 为 `needsAuth` / `error` / `loading`、无窗口、或工具调用失败，视为不可用。
2. **优先路径（MCP）**：对目标 tab 调用 `BuildProject`；失败时用 `GetBuildLog` 取错误。scheme / destination 以 Xcode 当前活动配置为准（需 Mac Catalyst 或 iPad Simulator 时，先在 Xcode 里选好再编）。
3. **回退路径（`xcodebuild`）**：仅当 MCP 不可用时使用：

```bash
# 编译 Mac Catalyst
xcodebuild -workspace MotionStudio.xcworkspace -scheme MotionStudioApp -configuration Debug -destination "generic/platform=macOS,variant=Mac Catalyst,name=Any Mac" ARCHS="arm64"

# 编译 iPad 模拟器
xcodebuild -workspace MotionStudio.xcworkspace -scheme MotionStudioApp -configuration Debug -destination "generic/platform=iOS Simulator" ARCHS="arm64"

# destination 不可用时，可以用下面命令查看 destinations
xcodebuild -workspace MotionStudio.xcworkspace -scheme MotionStudioApp -showdestinations
```

CI（GitHub Actions，macOS runner）执行：`sync_deps.sh` → 带 ASan 的 Ninja 构建 → ctest。tgfx 预编译库按 `build/tgfx_prebuilt/` 缓存，命中时 Ninja 跳过 tgfx 编译。

## 架构

三层结构，详见 `docs/architecture.md`（数据模型见 `docs/data-model.md`，时间轴求值见 `docs/timeline-evaluation.md`，渲染见 `docs/rendering.md`）：

1. **Core 层**（`src/` + `include/MotionStudio/`，纯 C++17 静态库 `core`）：动画数据模型、undo/redo、timeline 求值、序列化。**核心原则：Core 不知道任何渲染后端**——渲染管线为 `SceneEvaluator`（场景求值 → 不可变 `SceneState` 快照）→ `BuildCommands`（→ 扁平 `DrawCommandList`）→ `PlayCommands`（在抽象 `RenderAdapter` 上回放），适配器实现该接口消费命令。
2. **桥接层**（`bridge/`）：extern "C" 薄桥接（非 Swift C++ interop，因模板/unique_ptr 支持不成熟）。不透明句柄 `MSDocument`/`MSCanvas`，只做类型转换和指针传递，无业务逻辑。`DrawCommand` 不越过 C ABI 边界（画布 API 内部完成 evaluate→build→play）。返回的字符串一律经 `ms_string_free` 释放；无回调机制，Swift 侧靠 `revision` 计数器 + `@Observable` 订阅变化。
3. **适配器与应用层**：`adapter/tgfx/`（tgfx 的 Metal 后端：离屏快照 / MTKView 直渲）；`apps/MotionStudioApp/`（SwiftUI，经桥接头导入 `motionstudio_bridge.h`，链接 `gen_xcode/Products/` 下的 core/bridge/adapter 静态库，搜索路径配置在 `Base.xcconfig`）。

### 模块与依赖方向

模块即 `src/`、`include/MotionStudio/`、`tests/` 下的同名子目录，三者同构（`include/` 一类型一文件，不暴露第三方类型；非模板实现放 `src/`）：

| 模块 | 职责 |
|---|---|
| `common/` | 基础类型：`EntityId`、`FrameTime`/`FrameRate`、`Vec2`/`Mat3`/`Color`、`BezierPath`、`Expected` |
| `model/` | 数据模型：`Document`→`Composition`→`Layer`→`Shape`、`EntityIndex`、`PropertyPath` |
| `animation/` | `Animatable<T>`、`Keyframe`、`Easing`（贝塞尔缓动：牛顿+二分）、`Interpolator<T>` |
| `undo/` | `Command` 接口、`UndoManager`、内置命令集 |
| `render/` | `SceneEvaluator`、`SceneState`、`DrawCommand`、`RenderAdapter` 接口 |
| `serialization/` | `Serializer`、DTO（与运行时模型解耦的文件格式）、`SchemaMigrator` |

依赖单向，禁止反向：`common ← model ← animation ← undo`；`render` 依赖 model + animation；`serialization` 依赖 model。Core 库对外仅私有链接 nlohmann/json。

### 关键设计决策

- **时间 = 帧号整数**（`FrameTime = int64_t` + `FrameRate`），精确、可序列化、UI 吸附自然。
- **所有权是树，引用走 `EntityId`**；`EntityIndex` 提供 O(1) 寻址，undo 命令只持 ID 不持指针，安全解析目标（目标已删除时静默跳过）。
- **属性双态**：`Animatable<T>` 要么静态值，要么关键帧序列。
- **undo/redo = Command 模式**：支持合并（拖拽收敛为一个 undo 单元，`ms_document_end_merge_group` 关闭窗口）与组合；历史不持久化。
- **Lottie 导出是渲染管线的例外**：直接从模型转换以保留关键帧结构，而非消费 `DrawCommand`。

## 项目规范

`.claude/rules/` 下的规范（自动加载，必须遵守）：`coding-style.md`（命名/禁异常与 dynamic_cast/注释约定等）、`git-workflow.md`（分支命名、commit 信息、pre-commit 格式化、自动提交规则）、`testing.md`（GoogleTest 约定：不用 `EXPECT_THROW`、`Expected` 用 `hasValue()`/`error()` 断言、death test、round-trip 与 undo 一致性等专项模式）、`codegraph.md`（CodeGraph MCP 工具的使用规范）。

<!-- CODEGRAPH_START -->
CodeGraph 使用规范见 `.claude/rules/codegraph.md`。
<!-- CODEGRAPH_END -->