# Layer Effects UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在叶子层 Inspector 提供 Effects 列表编辑，并在时间轴展开已有关键帧的 effect 属性行。

**Architecture:** 不改现有 add/remove/move/set 命令。Bridge 只补 `ms_layer_effect_repeat_edge_at` 查询。App 侧 `EffectsInspector` 照抄 Masks/Fills 列表（倒序、加减、上下移），数值走 `NumberPropertyRow` + `effects[i].*`。时间轴用 `timelineEffectTracks` 挂进现有 `buildTimelineRows`。

**Tech Stack:** C++17 Bridge、GoogleTest、SwiftUI、`MotionDocumentCore`、`NumberPropertyRow`

**Spec:** `docs/superpowers/specs/2026-08-16-layer-effects-ui-design.md`

## Global Constraints

- 仅 Shape / Image / Text 显示 Effects 段与时间轴 effect 行；Group / Precomp 不显示
- 列表倒序：最上 = 数组最后一项 = 最后生效；`+` 仍 append
- 行标题用 `MS_EFFECT.label`，不编号
- `enabled` 用 `Toggle`，关掉后参数仍可改
- Gaussian Blur 暴露 `Repeat Edge` Toggle
- 不抽通用列表组件；不镜像 Swift effect 枚举
- 分发用 `type()` + `static_cast`；禁止 `dynamic_cast` 与 C++ 异常
- `if` / `switch` / `guard` 分支必须 `{}`；C++ 成员 `= {}` 初始化
- 代码和注释英语；Commit：英语一句、句号结尾、120 字符内、无其它标点；`git commit --only`；不 push
- 每完成一步立刻把本 plan 对应 `- [ ]` 改为 `- [x]` 并更新 Task `**Status:**`，与代码一并 commit
- 无新 XCTest；App 用手测清单验收
- Xcode 工程是 `PBXFileSystemSynchronizedRootGroup`，`Inspector/` 下新 Swift 文件自动进 target

---

## 文件结构

| 文件 | 职责 |
|---|---|
| `bridge/include/motionstudio_bridge.h` | 声明 `ms_layer_effect_repeat_edge_at` |
| `bridge/src/common/motionstudio_bridge_layer.cpp` | 查询 `repeatEdgePixels` |
| `bridge/tests/BridgeTest.cpp` | 查询用例 |
| `apps/.../Bridge/PropertyPath.swift` | `EffectProperty` 路径 |
| `apps/.../Model/MotionDocumentCore.swift` | effect 查询 / 命令薄包装 |
| `apps/.../Inspector/EffectsInspector.swift` | Effects 段 UI |
| `apps/.../Inspector/InspectorView.swift` | 叶子层挂上 Effects 段 |
| `apps/.../Timeline/Root/TimelineSupport.swift` | `timelineEffectTracks` |
| 本 plan + UI spec | 状态同步 |

---

### Task 1: Bridge repeat-edge 查询 + App 封装

**Status:** ✅ Done

**Files:**
- Modify: `bridge/include/motionstudio_bridge.h`（`ms_layer_effect_enabled_at` 后）
- Modify: `bridge/src/common/motionstudio_bridge_layer.cpp`
- Modify: `bridge/tests/BridgeTest.cpp`（`LayerEffectLifecycle` 后新测）
- Modify: `apps/MotionStudioApp/MotionStudioApp/Bridge/PropertyPath.swift`（`MaskProperty` 后）
- Modify: `apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift`

**Interfaces:**
- Consumes: `Layer::effects`、`GaussianBlurEffect::repeatEdgePixels`、已有 add/set 命令
- Produces: `bool ms_layer_effect_repeat_edge_at(MSDocument *, uint64_t layerId, int index)`
- Produces: `enum EffectProperty { brightness, contrast, blurriness }` + `path(at:)` / `actionLabel`
- Produces: `MotionDocumentCore.effectCount` / `effectType` / `effectEnabled` / `gaussianBlurRepeatEdge` 与对应命令方法

- [x] **Step 1: 写失败测试**

在 `bridge/tests/BridgeTest.cpp` 的 `TEST(BridgeCommandTest, LayerEffectLifecycle)` 之后插入：

```cpp
TEST(BridgeCommandTest, LayerEffectRepeatEdgeQuery) {
    MSDocument *document = ms_document_create();
    const uint64_t compositionId = ms_document_composition_id_at(document, 0);
    const uint64_t layerId = ms_command_add_rect_layer(document, compositionId);

    EXPECT_FALSE(ms_layer_effect_repeat_edge_at(document, layerId, 0));

    ms_command_add_gaussian_blur_effect(document, layerId);
    EXPECT_FALSE(ms_layer_effect_repeat_edge_at(document, layerId, 0));

    ms_command_set_gaussian_blur_repeat_edge(document, layerId, 0, true);
    EXPECT_TRUE(ms_layer_effect_repeat_edge_at(document, layerId, 0));

    ms_command_add_brightness_contrast_effect(document, layerId);
    EXPECT_FALSE(ms_layer_effect_repeat_edge_at(document, layerId, 1));

    ms_document_destroy(document);
}
```

- [x] **Step 2: 跑测试确认失败**

```bash
cmake --build build --target bridge_test
ctest --test-dir build -R LayerEffectRepeatEdgeQuery --output-on-failure
```

Expected: 编译失败，找不到 `ms_layer_effect_repeat_edge_at`。

- [x] **Step 3: 声明并实现查询**

`bridge/include/motionstudio_bridge.h` 在 `ms_layer_effect_enabled_at` 后加：

```c
// Gaussian Blur repeatEdgePixels at index; false when out of range or not a blur.
bool ms_layer_effect_repeat_edge_at(MSDocument *document, uint64_t layerId, int index);
```

`bridge/src/common/motionstudio_bridge_layer.cpp` 在 `ms_layer_effect_enabled_at` 后加（该文件已 `#include "MotionStudio/model/LayerEffect.h"`）：

```cpp
bool ms_layer_effect_repeat_edge_at(MSDocument *document, uint64_t layerId, int index) {
    DocumentLock guard(document);
    Layer *layer = FindLayer(document, layerId);
    if (layer == nullptr || index < 0 ||
        static_cast<size_t>(index) >= layer->effects.size()) {
        return false;
    }
    const motion::LayerEffect &effect = *layer->effects[static_cast<size_t>(index)];
    if (effect.type() != motion::LayerEffectType::GaussianBlur) {
        return false;
    }
    return static_cast<const motion::GaussianBlurEffect &>(effect).repeatEdgePixels;
}
```

- [x] **Step 4: 跑测试确认通过**

```bash
cmake --build build --target bridge_test
ctest --test-dir build -R 'LayerEffectLifecycle|LayerEffectRepeatEdgeQuery' --output-on-failure
```

Expected: 两测全绿。

- [x] **Step 5: 加 `EffectProperty` 与 `MotionDocumentCore` 包装**

`PropertyPath.swift` 在 `enum MaskProperty` 之后、`enum StyleProperty` 之前插入：

```swift
enum EffectProperty: String, CaseIterable {
    case brightness
    case contrast
    case blurriness

    func path(at index: Int) -> String {
        "effects[\(index)].\(rawValue)"
    }

    var actionLabel: String {
        switch self {
        case .brightness:
            "Brightness"
        case .contrast:
            "Contrast"
        case .blurriness:
            "Blurriness"
        }
    }
}
```

`MotionDocumentCore.swift` 在 `// MARK: - Mask / track matte queries` 之前插入：

```swift
    // MARK: - Layer effect queries

    func effectCount(layerID: UInt64) -> Int {
        Int(ms_layer_effect_count(handle, layerID))
    }

    func effectType(layerID: UInt64, index: Int) -> MS_EFFECT {
        ms_layer_effect_type_at(handle, layerID, Int32(index))
    }

    func effectEnabled(layerID: UInt64, index: Int) -> Bool {
        ms_layer_effect_enabled_at(handle, layerID, Int32(index))
    }

    func gaussianBlurRepeatEdge(layerID: UInt64, index: Int) -> Bool {
        ms_layer_effect_repeat_edge_at(handle, layerID, Int32(index))
    }
```

在 `addStrokeStyle` 之后插入命令包装：

```swift
    func addBrightnessContrastEffect(layerID: UInt64) {
        ms_command_add_brightness_contrast_effect(handle, layerID)
        changed()
    }

    func addGaussianBlurEffect(layerID: UInt64) {
        ms_command_add_gaussian_blur_effect(handle, layerID)
        changed()
    }

    func removeLayerEffect(layerID: UInt64, index: Int) {
        ms_command_remove_layer_effect(handle, layerID, Int32(index))
        changed()
    }

    func moveLayerEffect(layerID: UInt64, from fromIndex: Int, to toIndex: Int) {
        ms_command_move_layer_effect(handle, layerID, Int32(fromIndex), Int32(toIndex))
        changed()
    }

    func setLayerEffectEnabled(layerID: UInt64, index: Int, enabled: Bool) {
        ms_command_set_layer_effect_enabled(handle, layerID, Int32(index), enabled)
        changed()
    }

    func setGaussianBlurRepeatEdge(layerID: UInt64, index: Int, repeatEdge: Bool) {
        ms_command_set_gaussian_blur_repeat_edge(handle, layerID, Int32(index), repeatEdge)
        changed()
    }
```

- [x] **Step 6: Commit**

把本 Task 全部 checkbox 改为 `[x]`，`**Status:**` 改为 `✅ Done`，然后：

```bash
git commit --only \
  bridge/include/motionstudio_bridge.h \
  bridge/src/common/motionstudio_bridge_layer.cpp \
  bridge/tests/BridgeTest.cpp \
  apps/MotionStudioApp/MotionStudioApp/Bridge/PropertyPath.swift \
  apps/MotionStudioApp/MotionStudioApp/Model/MotionDocumentCore.swift \
  docs/superpowers/plans/2026-08-16-layer-effects-ui.md \
  -m "Expose layer effect queries and document commands on the app core."
```

---

### Task 2: EffectsInspector

**Status:** 未开始

**Files:**
- Create: `apps/MotionStudioApp/MotionStudioApp/Inspector/EffectsInspector.swift`
- Modify: `apps/MotionStudioApp/MotionStudioApp/Inspector/InspectorView.swift`（`TrackMatteInspector` 之后）

**Interfaces:**
- Consumes: Task 1 的 `MotionDocumentCore` effect API、`EffectProperty`、`MS_EFFECT.label` / `allCases`、`NumberPropertyRow`
- Produces: `struct EffectsInspector: View`（`core` / `layerID` / `isEditable` / `perform`）

- [ ] **Step 1: 新建 `EffectsInspector.swift`**

完整文件：

```swift
//
//  EffectsInspector.swift
//  MotionStudioApp
//
//  Post-process effect list: add/remove/reorder, enable toggle, and
//  keyframed brightness / contrast / blurriness. Newest effect is listed
//  first so the top row is applied last.
//

import MotionStudioBridging
import SwiftUI

struct EffectsInspector: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    @Environment(PlayheadClock.self) private var clock
    let isEditable: Bool

    private var playheadFrame: Int64 {
        clock.frame
    }

    let perform: (String, () -> Void) -> Void

    var body: some View {
        let _ = core.panelRevision
        let effects = Array((0 ..< core.effectCount(layerID: layerID)).reversed())
        HStack {
            Text("Effects")
                .font(.subheadline)
                .foregroundStyle(.secondary)
            Spacer()
            Menu {
                ForEach(MS_EFFECT.allCases) { type in
                    Button(type.label) {
                        addEffect(type)
                    }
                }
            } label: {
                Image(systemName: "plus")
            }
            .disabled(!isEditable)
        }
        ForEach(effects, id: \.self) { index in
            effectRow(index: index)
                .disabled(!isEditable)
        }
    }

    @ViewBuilder
    private func effectRow(index: Int) -> some View {
        let type = core.effectType(layerID: layerID, index: index)
        if type != .INVALID {
            VStack(alignment: .leading, spacing: 6) {
                HStack(spacing: 8) {
                    Text(type.label)
                        .font(.callout)
                    Toggle("", isOn: enabledBinding(index: index))
                        .labelsHidden()
                        .fixedSize()
                    Button {
                        moveEffect(index: index, visuallyUp: true)
                    } label: {
                        Image(systemName: "chevron.up")
                    }
                    .disabled(!isEditable || !canMoveEffect(index: index, visuallyUp: true))
                    .help("Bring effect later")
                    Button {
                        moveEffect(index: index, visuallyUp: false)
                    } label: {
                        Image(systemName: "chevron.down")
                    }
                    .disabled(!isEditable || !canMoveEffect(index: index, visuallyUp: false))
                    .help("Bring effect earlier")
                    Spacer(minLength: 0)
                    Button(role: .destructive) {
                        removeEffect(index: index)
                    } label: {
                        Image(systemName: "minus")
                    }
                }
                switch type {
                case .BRIGHTNESS_CONTRAST:
                    floatRow(index: index, property: .brightness, label: "Brightness")
                    floatRow(index: index, property: .contrast, label: "Contrast")
                case .GAUSSIAN_BLUR:
                    floatRow(index: index, property: .blurriness, label: "Blurriness")
                    Toggle("Repeat Edge", isOn: repeatEdgeBinding(index: index))
                case .INVALID:
                    EmptyView()
                }
            }
            .padding(.vertical, 2)
            .id("effect-row-\(index)-\(core.panelRevision)")
        }
    }

    private func floatRow(index: Int, property: EffectProperty, label: String) -> some View {
        let path = property.path(at: index)
        let hasKeyframe = hasKeyframeAtPlayhead(path: path)
        return NumberPropertyRow(label: label,
                                 value: core.evaluateFloat(entityID: layerID, path: path,
                                                           frame: playheadFrame),
                                 hasKeyframeAtPlayhead: hasKeyframe,
                                 isEditable: isEditable)
        { newValue in
            guard isEditable else {
                return
            }
            perform("Set \(label)") {
                writeFloat(path: path, value: newValue)
                core.endMergeGroup()
            }
        } onToggleKeyframe: { value in
            guard isEditable else {
                return
            }
            if hasKeyframeAtPlayhead(path: path) {
                perform("Delete Keyframe") {
                    core.removeKeyframe(entityID: layerID, path: path, frame: playheadFrame)
                }
            } else {
                perform("Add Keyframe") {
                    core.addKeyframeFloat(entityID: layerID, path: path,
                                          frame: playheadFrame, value: value)
                }
            }
        }
        .id("effect-\(path)-\(core.panelRevision)-\(hasKeyframe)")
    }

    private func hasKeyframeAtPlayhead(path: String) -> Bool {
        core.keyframeFrames(entityID: layerID, path: path).contains(playheadFrame)
    }

    private func writeFloat(path: String, value: Float) {
        if hasKeyframeAtPlayhead(path: path) {
            core.addKeyframeFloat(entityID: layerID, path: path,
                                  frame: playheadFrame, value: value)
        } else {
            core.setStaticFloat(entityID: layerID, path: path, value: value)
        }
    }

    private func enabledBinding(index: Int) -> Binding<Bool> {
        Binding {
            core.effectEnabled(layerID: layerID, index: index)
        } set: { newValue in
            guard isEditable else {
                return
            }
            perform("Set Effect Enabled") {
                core.setLayerEffectEnabled(layerID: layerID, index: index, enabled: newValue)
            }
        }
    }

    private func repeatEdgeBinding(index: Int) -> Binding<Bool> {
        Binding {
            core.gaussianBlurRepeatEdge(layerID: layerID, index: index)
        } set: { newValue in
            guard isEditable else {
                return
            }
            perform("Set Repeat Edge") {
                core.setGaussianBlurRepeatEdge(layerID: layerID, index: index, repeatEdge: newValue)
            }
        }
    }

    private func addEffect(_ type: MS_EFFECT) {
        guard isEditable else {
            return
        }
        switch type {
        case .BRIGHTNESS_CONTRAST:
            perform("Add Brightness Contrast") {
                core.addBrightnessContrastEffect(layerID: layerID)
            }
        case .GAUSSIAN_BLUR:
            perform("Add Gaussian Blur") {
                core.addGaussianBlurEffect(layerID: layerID)
            }
        case .INVALID:
            break
        }
    }

    private func removeEffect(index: Int) {
        guard isEditable else {
            return
        }
        perform("Remove Effect") {
            core.removeLayerEffect(layerID: layerID, index: index)
        }
    }

    private func canMoveEffect(index: Int, visuallyUp: Bool) -> Bool {
        let count = core.effectCount(layerID: layerID)
        guard count > 0, index >= 0, index < count else {
            return false
        }
        if visuallyUp {
            return index + 1 < count
        }
        return index > 0
    }

    private func moveEffect(index: Int, visuallyUp: Bool) {
        guard isEditable else {
            return
        }
        let count = core.effectCount(layerID: layerID)
        guard count > 0, index >= 0, index < count else {
            return
        }
        let toIndex = visuallyUp ? index + 1 : index - 1
        guard toIndex >= 0, toIndex < count else {
            return
        }
        perform("Move Effect") {
            core.moveLayerEffect(layerID: layerID, from: index, to: toIndex)
        }
    }
}
```

- [ ] **Step 2: 挂进 `InspectorView`**

在 `TrackMatteInspector(...)` 之后、`VStack` 结束之前插入：

```swift
                        if core.layerType(layerID) == .SHAPE ||
                            core.layerType(layerID) == .IMAGE ||
                            core.layerType(layerID) == .TEXT
                        {
                            EffectsInspector(core: core,
                                             layerID: layerID,
                                             isEditable: isEditable,
                                             perform: perform)
                        }
```

- [ ] **Step 3: 编译 App 确认通过**

优先 Xcode MCP：`user-xcode` ready 则 `XcodeListWindows` 拿到 `MotionStudio.xcworkspace` 的 `tabIdentifier`，再 `BuildProject`。MCP 不可用则：

```bash
xcodebuild -workspace MotionStudio.xcworkspace -scheme MotionStudioApp -configuration Debug \
  -destination "generic/platform=macOS,variant=Mac Catalyst,name=Any Mac" ARCHS="arm64"
```

Expected: BUILD SUCCEEDED。

- [ ] **Step 4: Commit**

勾选本 Task checkbox，`**Status:** ✅ Done`，然后：

```bash
git commit --only \
  apps/MotionStudioApp/MotionStudioApp/Inspector/EffectsInspector.swift \
  apps/MotionStudioApp/MotionStudioApp/Inspector/InspectorView.swift \
  docs/superpowers/plans/2026-08-16-layer-effects-ui.md \
  -m "Add the layer effects inspector for leaf layers."
```

---

### Task 3: 时间轴 effect 行

**Status:** 未开始

**Files:**
- Modify: `apps/MotionStudioApp/MotionStudioApp/Timeline/Root/TimelineSupport.swift`
- Modify: `docs/superpowers/specs/2026-08-16-layer-effects-ui-design.md`（状态改为已实现）

**Interfaces:**
- Consumes: `MotionDocumentCore.effectCount` / `effectType`、`EffectProperty.path(at:)` / `actionLabel`、`MS_EFFECT.label`
- Produces: `timelineEffectTracks(core:layerID:) -> [(path: String, label: String)]`

- [ ] **Step 1: 实现 `timelineEffectTracks`**

在 `timelineMaskTracks` 之后插入：

```swift
func timelineEffectTracks(core: MotionDocumentCore,
                          layerID: UInt64) -> [(path: String, label: String)]
{
    let layerType = core.layerType(layerID)
    guard layerType == .SHAPE || layerType == .IMAGE || layerType == .TEXT else {
        return []
    }
    var tracks: [(path: String, label: String)] = []
    for index in 0 ..< core.effectCount(layerID: layerID) {
        let type = core.effectType(layerID: layerID, index: index)
        let properties: [EffectProperty]
        switch type {
        case .BRIGHTNESS_CONTRAST:
            properties = [.brightness, .contrast]
        case .GAUSSIAN_BLUR:
            properties = [.blurriness]
        case .INVALID:
            continue
        }
        for property in properties {
            let path = property.path(at: index)
            if !core.keyframeFrames(entityID: layerID, path: path).isEmpty {
                tracks.append((path, "\(type.label) \(property.actionLabel)"))
            }
        }
    }
    return tracks
}
```

`timelineAnimatedPropertyPaths` 在 mask tracks 那一行后加：

```swift
    paths.append(contentsOf: timelineEffectTracks(core: core, layerID: layerID).map(\.path))
```

`buildTimelineRows` 在 mask tracks 循环后加：

```swift
        for track in timelineEffectTracks(core: core, layerID: layerID) {
            rows.append(timelinePropertyRow(core: core,
                                            layerID: layerID,
                                            path: track.path,
                                            label: track.label))
        }
```

- [ ] **Step 2: 编译 App 确认通过**

优先 Xcode MCP：`user-xcode` ready 则 `XcodeListWindows` 拿到 `MotionStudio.xcworkspace` 的 `tabIdentifier`，再 `BuildProject`。MCP 不可用则：

```bash
xcodebuild -workspace MotionStudio.xcworkspace -scheme MotionStudioApp -configuration Debug \
  -destination "generic/platform=macOS,variant=Mac Catalyst,name=Any Mac" ARCHS="arm64"
```

Expected: BUILD SUCCEEDED。

- [ ] **Step 3: 手测清单（实现者在 App 里点）**

1. Shape / Image / Text：`+` 能加 Brightness Contrast 与 Gaussian Blur；后加的在最上。
2. 改 Brightness / Blurriness，画布跟着变；`enabled` 关掉则复原，参数仍能改。
3. 两条 effect 上下移后，BC→Blur 与 Blur→BC 观感不同。
4. 菱形打关键帧后时间轴出现 `Brightness Contrast Brightness` 或 `Gaussian Blur Blurriness`；undo 能撤。
5. Repeat Edge 开关能写回（边缘渗出 vs 钳制）。

- [ ] **Step 4: 更新 spec 状态并 Commit**

`docs/superpowers/specs/2026-08-16-layer-effects-ui-design.md` 把 `状态：已批准` 改为 `状态：已实现`。

本 Task checkbox 全勾，`**Status:** ✅ Done`，然后：

```bash
git commit --only \
  apps/MotionStudioApp/MotionStudioApp/Timeline/Root/TimelineSupport.swift \
  docs/superpowers/specs/2026-08-16-layer-effects-ui-design.md \
  docs/superpowers/plans/2026-08-16-layer-effects-ui.md \
  -m "Show animated layer effect properties on the timeline."
```

---

## Spec 覆盖对照

| Spec | Task |
|---|---|
| `ms_layer_effect_repeat_edge_at` + BridgeTest | 1 |
| `EffectProperty` + `MotionDocumentCore` 包装 | 1 |
| `EffectsInspector` 倒序 / Menu / Toggle / 数值行 | 2 |
| 仅叶子层、挂在 Track Matte 之后 | 2 |
| `timelineEffectTracks` 只展开已有关键帧 | 3 |
| Group/Precomp 不显示、不抽通用列表、不升 schema | 全局约束 |
