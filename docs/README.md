# Motion Studio 文档

Motion Studio 是一个 2D 动效（Motion Graphics）动画制作工具：图层 + Transform + 形状 + 关键帧动画，核心层 C++17，第一阶段应用层为 macOS + iPadOS（SwiftUI + MetalKit，tgfx 渲染）。

## 文档索引

| 文档 | 内容 |
|---|---|
| [architecture.md](architecture.md) | 总体架构：三层分层（Core / Bridge / App）、模块划分、目录结构、extern "C" 桥接层设计、依赖与构建 |
| [data-model.md](data-model.md) | 动画数据结构：时间表示、EntityId、Document→Composition→Layer→Shape 层级、`Animatable<T>`、Transform 与父子继承、Command 模式 undo/redo、序列化与 DTO |
| [figma-to-motionstudio.md](figma-to-motionstudio.md) | Figma Design / Motion → MotionStudio：毫秒↔帧、旋转符号与 Design+Motion 关键帧 |
| [superpowers/specs/2026-08-26-layer-order-subtree-contiguity-design.md](superpowers/specs/2026-08-26-layer-order-subtree-contiguity-design.md) | 图层顺序子树连续性：扁平 `layers[]` 保持后代紧贴父级，Core 集中强制 |
| [superpowers/specs/2026-09-02-shapepath-resize-preview-matrix-design.md](superpowers/specs/2026-09-02-shapepath-resize-preview-matrix-design.md) | ShapePath 缩放拖动：Canvas 局部预览矩阵，松手 bake 几何（[plan](superpowers/plans/2026-09-02-shapepath-resize-preview-matrix.md)） |
| [superpowers/specs/2026-08-21-geometry-revision-cache-design.md](superpowers/specs/2026-08-21-geometry-revision-cache-design.md) | VectorNetwork / BezierPath 进程内 revision：CompileFillFaces 与 tgfx::Path 身份缓存 |
| [superpowers/specs/2026-08-19-stroke-dash-design.md](superpowers/specs/2026-08-19-stroke-dash-design.md) | Stroke dash + Cap/Join Inspector（Trim 后再 `MakeDash`） |
| [superpowers/specs/2026-08-19-svg-import-design.md](superpowers/specs/2026-08-19-svg-import-design.md) | SVG 导入：tgfx `SVGDOM` → Core 图层树 + `VectorNetwork`（独立 `svg_import` 库） |
| [superpowers/specs/2026-08-20-svg-mask-track-matte-design.md](superpowers/specs/2026-08-20-svg-mask-track-matte-design.md) | SVG `mask=` → track matte；Group PAG isolation Precomp 包装 |
| [superpowers/specs/2026-08-19-svg-import-ui-design.md](superpowers/specs/2026-08-19-svg-import-ui-design.md) | SVG 导入 UI：File 菜单 + `UIDocumentPicker`，对标图片导入 |
| [superpowers/specs/2026-08-19-layer-group-ungroup-design.md](superpowers/specs/2026-08-19-layer-group-ungroup-design.md) | 图层编组/解组 + 底部时间轴左侧 Layer 树缩进 |
| [superpowers/specs/2026-08-19-image-group-corner-radius-design.md](superpowers/specs/2026-08-19-image-group-corner-radius-design.md) | Image / Group 圆角：容器或子层 AABB 裁剪，Group 有圆角/mask/matte 才 BeginLayer |
| [timeline-evaluation.md](timeline-evaluation.md) | 时间轴与曲线求值：关键帧求值流程、贝塞尔缓动（牛顿+二分）、空间插值、Precomp 时间映射、缓存策略 |
| [rendering.md](rendering.md) | 渲染抽象与导出：SceneEvaluator → SceneState → DrawCommand → RenderAdapter 流水线、Metal 适配器、PAG/序列帧/MP4 导出边界、线程模型 |
| [color-source-effect.md](color-source-effect.md) | ColorSourceEffect / RenderCache：过程色填充、离屏路径、pipeline 指纹与 UBO 三缓冲、调用约定 |
| [pag-runtime-filter.md](pag-runtime-filter.md) | RuntimeFilter / BrightnessContrast / GaussianBlur：图层后处理滤镜基类、pipeline 缓存、endLayer 链 |
| [superpowers/specs/2026-08-16-layer-effects-design.md](superpowers/specs/2026-08-16-layer-effects-design.md) | Layer effects：叶子层后处理链、求值快照、BeginLayer/EndLayer isolation |
| [superpowers/specs/2026-08-16-layer-effects-ui-design.md](superpowers/specs/2026-08-16-layer-effects-ui-design.md) | Layer effects UI：Inspector Effects 段 + 时间轴关键帧行 |
| [superpowers/specs/2026-08-16-layer-styles-design.md](superpowers/specs/2026-08-16-layer-styles-design.md) | Layer styles：Drop Shadow + Outer Glow + Stroke（Gradient Overlay 另开） |
| [libpag-rendering-optimization-notes.md](libpag-rendering-optimization-notes.md) | libpag 渲染优化对照：contentVersion / 静帧缓存 / Snapshot / maxFrameRate 等，及对 MotionStudio 持续播放高 CPU 的改进建议 |
| [edit-drag-responsiveness.md](edit-drag-responsiveness.md) | 编辑拖动跟手：`revision` vs `panelRevision`、Timeline merge 推迟、点文本缓存；SwiftUI→UIKit 后删除清单 |
| [superpowers/specs/2026-07-31-pag-export-design.md](superpowers/specs/2026-07-31-pag-export-design.md) | PAG 导出设计；**§0 已落地**：唯一 tgfx 源 + Metal 预编译 + `pag_codec`（base+codec），及取舍原因 |
| [superpowers/specs/2026-07-29-playback-cpu-optimization-design.md](superpowers/specs/2026-07-29-playback-cpu-optimization-design.md) | 播放预览 CPU：Phase 1/2 已落地；Phase 3 GPU Snapshot 取消及原因 |
| [superpowers/specs/2026-07-29-timeline-uikit-migration-design.md](superpowers/specs/2026-07-29-timeline-uikit-migration-design.md) | 底部 Timeline 已迁 UIKit（按职责分子目录）；PlayheadClock listener + 保留 @Observable |
| [development-plan.md](development-plan.md) | 开发计划：M0–M4 里程碑（约 18 周）、风险清单、测试策略 |

## 阅读顺序

1. **architecture.md** — 先建立全局视角
2. **data-model.md** — 项目地基，最重要
3. **timeline-evaluation.md** — 数据如何随时间变化
4. **rendering.md** — 数据如何变成画面
5. **development-plan.md** — 何时做什么

## 核心设计决策速览

- **时间 = 帧号整数**（`FrameTime = int64_t` + `FrameRate`），精确、可序列化、UI 吸附自然
- **所有权是树，引用走 EntityId**；`EntityIndex` 提供 O(1) 寻址，支撑 undo 命令安全解析目标
- **属性双态**：`Animatable<T>` 要么静态值，要么关键帧序列；插值策略由 `Interpolator<T>` trait 注入
- **undo/redo = Command 模式**：命令只持 ID 不持指针，支持合并（拖拽收敛为一个 undo 单元）与组合，历史不持久化
- **核心渲染无关**：Core 输出扁平 `DrawCommandList`，Metal/导出器均为适配器；PAG 导出从模型直转以保留关键帧结构
- **Swift 桥接用 extern "C"**：薄、可调试、未来 WASM/移动端可复用
