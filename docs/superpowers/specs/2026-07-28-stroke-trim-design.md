# Phase C：Stroke Trim — 设计说明

日期：2026-07-28  
状态：已完成（能力已有；补回归测试）  
分支：`feature/0x1306a94_path_animation`  
总览：[path-animation-roadmap](./2026-07-28-path-animation-roadmap.md)

## 结论

**Stroke Trim 产品能力已落地**，本阶段不新做功能，只做文档定界 + 回归测试。

已有链路：

| 层 | 现状 |
|---|---|
| 模型 | `StrokeStyle::{trimStart, trimEnd, trimOffset}`（0–1 / 0–1 / 度） |
| 求值 | `SceneEvaluator` → `StrokeOptions` |
| 渲染 | `TgfxCanvasAdapter::TrimmedPath`（tgfx `PathEffect::MakeTrim`） |
| UI | `StrokesInspector` 三行 + 时间轴 trim 轨道 + float 关键帧 |
| Bridge | 通用 float 属性通道（已有单测） |

## 目标

1. 写明语义与非目标，避免与后续 AE 式 `ShapeTrimPath` 混淆  
2. 适配器回归：部分窗口 / 空窗口不画  

## 非目标（后置）

- `ShapeTrimPath` 形状修饰器（裁剪 Fill+Stroke 的 AE 语义）  
- Core 自研弧长测长 / 把 trim 挪出适配器  
- 新 App UI  

## 语义（Stroke 专用）

- 只裁**描边**几何；同层 Fill 仍画完整轮廓  
- `trimEnd - trimStart < 1` 时才应用 trim；全长窗口（差 ≥ 1）等同不裁  
- `trimOffset`（度）旋转窗口；`start == end` → 空路径，不绘制  
- `start > end`（经 offset 归一后）按 tgfx 语义环绕  

## 测试

- `TgfxRenderAdapterTest`：水平线 stroke + 半段 trim → 中点一侧有色、一侧背景  
- 空 trim 窗口 → 不画 stroke  

## 进度

Core/适配器测绿 → `core-done`；无新 UI → App 列直接 `done`。
