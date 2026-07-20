# 开发计划

Motion Studio 分 5 个里程碑推进，总计约 18 周。每个里程碑有明确交付物与可验证的验收标准。设计文档：[architecture.md](architecture.md)、[data-model.md](data-model.md)、[timeline-evaluation.md](timeline-evaluation.md)、[rendering.md](rendering.md)。

## 里程碑总览

| 里程碑 | 周期 | 主题 | 核心验收 |
|---|---|---|---|
| M0 | 2 周 | 构建体系 | `cmake --build && ctest` 通过，Swift 能调通桥接 hello world |
| M1 | 4 周 | 数据模型 + Undo | 1000 次随机操作 + undo/redo 无崩溃无泄漏（ASan） |
| M2 | 3 周 | Timeline 求值 | 缓动曲线与 CSS 参考误差 < 1e-5；100 图层单帧求值 < 2ms |
| M3 | 6 周 | macOS UI + Metal | 关键帧动画 60fps 播放；拖拽编辑 + undo/redo 正确 |
| M4 | 3 周 | 导出 | Lottie 在 lottiefiles 播放器正确播放；视觉一致性 > 95% |

---

## M0 — 构建体系（2 周）

**交付物**
- 顶层 CMake：`core` 静态库（C++17）+ `bridge` 库 + `tests`
- GoogleTest 测试框架接入（FetchContent + `gtest_discover_tests`），一个占位测试通过
- GitHub Actions CI：macOS runner 编译 + ctest
- Xcode 工程骨架：链接 core + bridge，Swift 端通过 modulemap 调用 `ms_version()` 成功

**验收**
```bash
cmake -B build && cmake --build build && cd build && ctest   # 全绿
# Xcode 运行 macOS app，界面打印 core 库版本号
```

**关键点**：`POSITION_INDEPENDENT_CODE ON`；nlohmann/json 随仓库引入；CI 缓存编译产物。

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

## M3 — macOS UI 壳 + Metal 渲染（6 周）

**交付物**
- 四区布局：左·图层面板 / 中·画布 / 右·属性检查器 / 下·时间轴
- `CanvasView`（NSView + CAMetalLayer）+ CVDisplayLink 渲染循环
- `MetalRenderAdapter`：DrawCommand → Metal draw call；CPU 路径细分（填充耳切 + 描边轮廓化）
- 时间轴 UI：刻度/播放头、关键帧菱形显示、拖拽移动、缓动预设切换
- 属性检查器：Transform 五属性编辑、添加/删除关键帧
- 文件打开/保存（.msjson）+ ⌘Z / ⇧⌘Z 绑定 UndoManager
- 桥接层扩充到覆盖 UI 所需全部函数（按模块分头文件）

**验收**
- 端到端：创建矩形 → position 加两个关键帧（EaseOut）→ 空格播放 → 矩形沿缓动运动，60fps
- 拖拽关键帧后 ⌘Z 回到原位；连续拖拽合并为一个 undo 单元
- 10 图层 + 100 关键帧场景播放不掉帧

**后续优化项（不阻塞验收）**：求值双缓冲移至渲染线程、曲线编辑器（手柄拖拽）、多选。

---

## M4 — 导出（3 周）

**交付物**
- `export/`：LottieExporter（Document → Lottie JSON，保留关键帧结构）
- 序列帧 PNG 导出（Metal offscreen render target 逐帧回读）
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
- **桥接层**：Swift XCTest 验证 C ABI 调用与内存释放
- **渲染**：snapshot 测试——固定场景渲染截图 vs 参考图，容差 1%
- **导出**：Lottie 用例集 + lottie-web 逐帧像素对比

## M4 之后的候选方向（未排期）

移动端/Web UI 壳、GPU 路径细分、表达式/脚本、渐变与图像填充、遮罩与混合模式完整集、Lottie 导入、协同编辑。
