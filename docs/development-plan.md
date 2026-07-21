# 开发计划

Motion Studio 分 5 个里程碑推进，总计约 18 周。每个里程碑有明确交付物与可验证的验收标准。设计文档：[architecture.md](architecture.md)、[data-model.md](data-model.md)、[timeline-evaluation.md](timeline-evaluation.md)、[rendering.md](rendering.md)。

**测试原则**：M0–M2 是纯 C++ 核心层，不依赖 UI 与渲染后端，全部功能正确性由 GoogleTest 单元测试（CTest 驱动）验证；性能基准（如 M2 单帧求值 < 2ms）作为独立 benchmark 用例，CI 仅记录不阻断。桥接层同样有 GoogleTest 覆盖（BridgeTest）。M3 起用 XCUITest 做端到端验证（新建文档 → 编辑 → undo），离屏渲染正确性由 tgfx 适配器快照测试保障。

## 里程碑总览

| 里程碑 | 周期 | 主题 | 核心验收 |
|---|---|---|---|
| M0 | 2 周 | 构建体系 | `cmake --build && ctest` 通过 |
| M1 | 4 周 | 数据模型 + Undo | 1000 次随机操作 + undo/redo 无崩溃无泄漏（ASan） |
| M2 | 3 周 | Timeline 求值 | 缓动曲线与 CSS 参考误差 < 1e-5；100 图层单帧求值 < 2ms |
| M3 | 6 周 | macOS + iPadOS UI 壳 | 关键帧动画播放；拖拽编辑 + undo/redo 正确 |
| M4 | 3 周 | 导出 | Lottie 在 lottiefiles 播放器正确播放；视觉一致性 > 95% |

---

## M0 — 构建体系（2 周）

**交付物**
- 顶层 CMake：`core` 静态库（C++17）+ `tests`
- 依赖同步流程：[depctl](https://github.com/0x1306a94/depctl) + DEPS 声明（GoogleTest 同步至 `third_party/googletest`），CMake `add_subdirectory` + `gtest_discover_tests` 接入，一个占位测试通过
- GitHub Actions CI：macOS runner 执行 `sync_deps.sh` → 编译 → ctest

**验收**
```bash
./sync_deps.sh
cmake -B build -G Ninja && cmake --build build && cd build && ctest   # 全绿
```

**关键点**：`POSITION_INDEPENDENT_CODE ON`；第三方依赖统一走 DEPS 声明（不入库）；CI 缓存编译产物。

---

## M1 — 数据模型 + Undo（4 周）

**交付物**
- `common/`：EntityId、Time、Math（Vec2/Mat3/Color）、BezierPath
- `model/`：Document + EntityIndex、Composition、Layer + Transform、LayerContent 五态、Shape 元素集、PropertyPath 解析
- `animation/`：Animatable\<T\>（float/Vec2/Color/BezierPath）、Keyframe、Easing、Interpolator 特化
- `undo/`：Command、UndoManager（双栈 + 合并窗口）、CompositeCommand、8 个核心命令（AddLayer / RemoveLayer / MoveLayer / SetStaticValue / AddKeyframe / RemoveKeyframe / MoveKeyframe / SetEasing）
- `serialization/`：DTO、Serializer、SchemaMigrator 骨架，JSON v1 schema

**验收**
- 单元测试链：建文档 → 加图层 → 设属性 → 加关键帧 → undo/redo 循环 → 序列化 round-trip 等价
- 模糊测试：1000 次随机命令 + 随机 undo/redo，ASan 下无崩溃无泄漏
- setParent 环检测测试
- debug 断言：undo 前后序列化 hash 一致

**已知取舍**：BezierPath 插值 M1 要求两关键帧顶点数一致（不一致时报错），自动顶点匹配留到 M2。

---

## M2 — Timeline 求值（3 周）

**交付物**
- `animation/`：贝塞尔缓动求解器（牛顿 + 二分）、空间贝塞尔插值
- `render/`：SceneEvaluator（世界变换、parent 链、opacity 继承、in/outPoint 裁剪、ShapeGroup 展开）、SceneState
- Precomp 递归求值与时间映射
- BezierPath 顶点自动匹配（短路径边上插点）
- 性能基准测试

**验收**
- 缓动求值 vs CSS `cubic-bezier()` 参考：误差 < 1e-5（含 EaseIn/EaseOut/极端手柄用例）
- ≥ 3 层嵌套 Precomp 求值正确性测试
- 基准：100 图层 × 50 关键帧，单帧求值 < 2ms（release，Apple Silicon）；不达标则引入 EvaluationCache

---

## M3 — macOS + iPadOS UI 壳（6 周）

**交付物**（首版已落地，端到端验证待补齐）
- 文档基建：`MotionDocument`（`ReferenceFileDocument`）+ `MotionDocumentCore`（桥接层的 Swift facade），.mjson 文档读写；`DocumentGroup` 多文档窗口，iPadOS 文档浏览器
- 四区布局（SwiftUI，macOS/iPadOS 共享视图代码，仅布局容器分平台）：项目面板 / 画布 / 属性检查器 / 时间轴（含图层栈）
- 画布：`MTKView` + `TgfxOnScreenAdapter` 直渲；`CADisplayLink` 按合成帧率播放，暂停时按需重绘
- 时间轴：刻度尺 + 播放头（拖拽 scrub）、关键帧菱形拖拽移动（core 合并窗口 + 系统 undo 单次注册）、右键菜单切缓动预设/删关键帧
- 检查器：Transform 属性编辑（position X/Y、rotation、opacity），播放头处加关键帧
- 项目面板：素材空状态 + 合成列表 + 建层工具栏（创建矩形/椭圆/删除）；图层栈（名称 + 可见性/锁定开关）移入时间轴左列
- Undo：core 命令栈镜像到系统 `UndoManager`（⌘Z / ⇧⌘Z / 三指扫动），拖拽结束 `endDrag` 关合并窗口
- 桥接层：extern "C"（文档 / undo / 查询 / 命令 / 画布），Swift 经桥接头导入；`apps/MotionStudio.xcworkspace` 组合 CMake 生成的 `gen_xcode` 与应用工程
- XCUITest 端到端：⌘N 新建 → 加形状 → undo 移除

**验收**
- 端到端：创建矩形 → position 加两个关键帧（EaseOut）→ 播放 → 矩形沿缓动运动
- 拖拽关键帧后 ⌘Z 回到原位；连续拖拽合并为一个 undo 单元
- 10 图层 + 100 关键帧场景播放不掉帧

**后续优化项（不阻塞验收）**：求值双缓冲移至渲染线程、曲线编辑器（手柄拖拽）、多选、多属性关键帧轨道、iPad 触摸手势细化。

---

## M4 — 导出（3 周）

**交付物**
- `export/`：LottieExporter（Document → Lottie JSON，保留关键帧结构）
- 序列帧 PNG 导出（`TgfxRenderAdapter` 离屏渲染逐帧 `ReadPixels`，tgfx 自带 PNG 编码）
- 一致性验证工具：同一场景，内部渲染 vs lottie-web 渲染逐帧像素对比

**验收**
- 导出的 JSON 在 lottiefiles.com 播放器正确播放（矩形/椭圆动画、缓动、图层变换用例集）
- 像素对比视觉一致性 > 95%
- 序列帧导出 1080p × 3s @ 30fps 无错帧

---

## 风险清单

| # | 风险 | 级别 | 缓解 |
|---|---|---|---|
| 1 | Undo 一致性：undo 时目标实体可能已被删除 | 高 | 命令经 EntityIndex 校验存在性，不存在则跳过；Remove 命令持有 `unique_ptr` 所有权转移；debug 下序列化 hash 比对 |
| 2 | BezierPath 关键帧顶点数不匹配无法插值 | 中 | M1 强制一致，M2 实现顶点插入匹配（AE/Lottie 标准做法） |
| 3 | 桥接层函数膨胀 | 中 | 桥接层保持极薄（仅类型转换，无业务逻辑），按模块分头文件；简单值类型（Vec2/Color）后续可用 Swift C++ interop 补充 |
| 4 | 渲染抽象的栈式假设限制未来后端 | 中 | 覆盖 Metal/GL/CoreGraphics/Skia 已足够；Vulkan 类后端出现时再演化（YAGNI） |
| 5 | UI 线程与求值竞争 | 中 | 首版主线程同步；优化阶段用 SceneState 值快照双缓冲，Document 不加锁 |
| 6 | 跨 C ABI 边界的内存释放 | 低 | 所有 C ABI 返回的字符串/缓冲区配 `ms_free_*`，由分配方释放 |
| 7 | Precomp 时间映射算错 | 低 | 固定公式 `innerTime = (outer - inPoint) × timeStretch + startTime`，3 层嵌套专项测试 |

## 测试策略

- **Core 层**：GoogleTest 单元测试，CI 每次提交运行，ASan 开启
- **序列化**：round-trip 模糊测试——随机生成文档 → 序列化 → 反序列化 → 深度对比
- **桥接层**：GoogleTest（BridgeTest）覆盖 C ABI 调用、undo 往返与空句柄安全
- **渲染**：snapshot 测试——固定场景渲染截图 vs 参考图，容差 1%
- **应用层**：XCUITest 端到端（新建文档 → 建形状 → undo），需给执行终端授予辅助功能权限或在 Xcode 中运行

## M4 之后的候选方向（未排期）

Web/Android UI 壳、表达式/脚本、渐变与图像填充、遮罩与混合模式完整集、Lottie 导入、协同编辑。
