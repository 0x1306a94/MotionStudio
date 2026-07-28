# 路径动画总览（Roadmap）

日期：2026-07-28  
状态：完成（A/C/B/D 均已完成）  
分支：`feature/0x1306a94_path_animation`

## 目标

为 Motion Studio 分阶段补齐四类路径相关动画能力（最终都要支持）：

| Phase | 能力 | 说明 |
|---|---|---|
| **A** | Path Morph | ShapePath / Mask 的 `BezierPath` 关键帧形变 |
| **C** | Trim Path | 描边可见区间（start/end/offset）动画 |
| **B** | Motion Path | Position 空间轨迹可视化与曲线调节 |
| **D** | Follow Path | 图层沿路径运动，可选朝向 |

顺序固定为 **A → C → B → D**（依赖少、复用高 → 体验增强）。

## 工作方式

| 环节 | 约定 |
|---|---|
| 文档 | 每阶段先 design spec → implementation plan → 再编码 |
| Core / Bridge | 单测通过后自动 commit（不 push） |
| App UI | 改完后由人机验证；验证通过再标 `done` 并开下一阶段 |
| 进度 | 只维护本文件「进度表」 |

状态枚举：`todo` → `in-progress` → `core-done` → `ui-pending-verify` → `done`

## 进度表

| Phase | 能力 | Core/Bridge | App UI | 备注 |
|---|---|---|---|---|
| A | Path Morph | `done` | `done` | 详见 [path-morph-design](./2026-07-28-path-morph-design.md) |
| C | Trim Path | `done` | `done` | Stroke Trim 已有能力 + 回归测。见 [stroke-trim-design](./2026-07-28-stroke-trim-design.md)。`ShapeTrimPath` 修饰器后置 |
| B | Motion Path | `done` | `done` | 画布轨迹 + Inspector 曲线板。见 [motion-path-design](./2026-07-28-motion-path-design.md) |
| D | Follow Path | `done` | `done` | [design](./2026-07-28-follow-path-design.md) · [plan](../plans/2026-07-28-follow-path.md) |

## Phase 占位范围

### A — Path Morph

已完成。详见 Phase A design。

### C — Stroke Trim

- **已有**：`StrokeStyle` trim + 求值 + 适配器裁剪 + Inspector/时间轴。
- **本阶段**：文档定界 + 适配器回归测试；无新 UI。
- **后置**：AE 式 `ShapeTrimPath` 修饰器（Fill+Stroke）。
- 详见 [stroke-trim-design](./2026-07-28-stroke-trim-design.md)。

### B — Motion Path

已完成：B1 API + B2 画布轨迹 / Inspector `CubicBezierPad` 调节。见 [motion-path-design](./2026-07-28-motion-path-design.md)。

### D — Follow Path

已完成（人机验证通过）。见 [follow-path-design](./2026-07-28-follow-path-design.md)。

## 文档索引

| 文档 | 用途 |
|---|---|
| 本文件 | 总览 + 进度 |
| [2026-07-28-path-morph-design.md](./2026-07-28-path-morph-design.md) | Phase A 设计 |
| [2026-07-28-path-morph.md](../plans/2026-07-28-path-morph.md) | Phase A 实现计划 |
| [2026-07-28-motion-path-design.md](./2026-07-28-motion-path-design.md) | Phase B 设计 |
| [2026-07-28-motion-path.md](../plans/2026-07-28-motion-path.md) | Phase B 实现计划 |
| [2026-07-28-follow-path-design.md](./2026-07-28-follow-path-design.md) | Phase D 设计 |
| [2026-07-28-follow-path.md](../plans/2026-07-28-follow-path.md) | Phase D 实现计划；`ShapeTrimPath` 另开 |
