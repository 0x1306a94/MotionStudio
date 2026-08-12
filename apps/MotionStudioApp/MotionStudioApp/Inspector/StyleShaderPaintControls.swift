//
//  StyleShaderPaintControls.swift
//  MotionStudioApp
//
//  Shared Color/Gradient/Shader paint-mode controls for Fill and Stroke inspector rows.
//

import MotionStudioBridging
import SwiftUI

struct StyleShaderPaintControls<BelowPaintMode: View>: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    let styleIndex: Int
    let playheadFrame: Int64
    let isEditable: Bool
    let actionPrefix: String
    let perform: (String, () -> Void) -> Void
    @ViewBuilder let belowPaintMode: () -> BelowPaintMode

    @State private var editingShaderID: UInt64?

    var body: some View {
        let _ = core.panelRevision
        let mode = paintMode
        let shaderID = core.styleShaderID(layerID: layerID, index: styleIndex)

        VStack(alignment: .leading, spacing: 6) {
            Picker("Paint", selection: paintModeBinding) {
                ForEach(MS_PAINT_MODE.allCases) { tag in
                    Text(tag.label).tag(tag)
                }
            }
            .pickerStyle(.segmented)
            .disabled(!isEditable)

            // Title / blend / reorder sit here so Color / Gradient / Shader share one layout.
            belowPaintMode()

            if mode == .GRADIENT {
                gradientControls
            } else if mode == .SHADER {
                HStack(spacing: 8) {
                    Picker("Shader", selection: shaderBinding) {
                        Text("Select Shader…").tag(UInt64(0))
                        ForEach(core.shaderIDs(), id: \.self) { id in
                            Text(core.shaderName(id)).tag(id)
                        }
                    }
                    .labelsHidden()
                    .pickerStyle(.menu)
                    .disabled(!isEditable || core.shaderIDs().isEmpty)
                    // Menu pickers cache option titles; recreate when the library changes.
                    .id("style-shader-\(styleIndex)-\(core.panelRevision)")

                    Button("Edit Source") {
                        if shaderID != 0 {
                            editingShaderID = shaderID
                        }
                    }
                    .font(.caption)
                    .buttonStyle(.borderless)
                    .disabled(!isEditable || shaderID == 0)
                }

                if shaderID != 0 {
                    ForEach(0 ..< core.shaderUniformCount(shaderID), id: \.self) { uniformIndex in
                        uniformRow(shaderID: shaderID, uniformIndex: uniformIndex)
                    }
                }
            }
        }
        .sheet(item: editingShaderItem) { box in
            ShaderEditorSheet(core: core, shaderID: box.id, perform: perform) {
                editingShaderID = nil
            }
        }
    }

    private var paintMode: MS_PAINT_MODE {
        let mode = core.stylePaintMode(layerID: layerID, index: styleIndex)
        return mode == .INVALID ? .COLOR : mode
    }

    private var paintModeBinding: Binding<MS_PAINT_MODE> {
        Binding {
            paintMode
        } set: { newValue in
            guard isEditable else { return }
            if newValue == .SHADER {
                let shaders = core.shaderIDs()
                guard let first = shaders.first else { return }
                let current = core.styleShaderID(layerID: layerID, index: styleIndex)
                let target = current == 0 ? first : current
                perform("Set \(actionPrefix) Paint Mode") {
                    _ = core.setStylePaintMode(layerID: layerID, index: styleIndex, mode: .SHADER,
                                               shaderID: target)
                }
            } else if newValue == .GRADIENT {
                perform("Set \(actionPrefix) Paint Mode") {
                    _ = core.setStylePaintMode(layerID: layerID, index: styleIndex, mode: .GRADIENT,
                                               shaderID: 0)
                }
            } else {
                perform("Set \(actionPrefix) Paint Mode") {
                    _ = core.setStylePaintMode(layerID: layerID, index: styleIndex, mode: .COLOR,
                                               shaderID: 0)
                }
            }
        }
    }

    private var shaderBinding: Binding<UInt64> {
        Binding {
            core.styleShaderID(layerID: layerID, index: styleIndex)
        } set: { newValue in
            guard isEditable, newValue != 0 else { return }
            perform("Bind \(actionPrefix) Shader") {
                _ = core.setStylePaintMode(layerID: layerID, index: styleIndex, mode: .SHADER,
                                           shaderID: newValue)
            }
        }
    }

    @ViewBuilder
    private var gradientControls: some View {
        let gradientType = resolvedGradientType
        Picker("Type", selection: gradientTypeBinding) {
            ForEach(MS_GRADIENT_TYPE.allCases) { tag in
                Text(tag.label).tag(tag)
            }
        }
        .pickerStyle(.menu)
        .disabled(!isEditable)
        // Menu pickers cache the title; remount when undo/redo changes the type.
        .id("style-gradient-type-\(styleIndex)-\(gradientType.rawValue)")

        vec2PropertyRow(label: "Start", path: StyleProperty.gradientStart(styleIndex: styleIndex))
        vec2PropertyRow(label: "End", path: StyleProperty.gradientEnd(styleIndex: styleIndex))
        if gradientType == .CONIC {
            floatPropertyRow(label: "Start Angle",
                             path: StyleProperty.gradientStartAngle(styleIndex: styleIndex))
            floatPropertyRow(label: "End Angle",
                             path: StyleProperty.gradientEndAngle(styleIndex: styleIndex))
        }

        HStack {
            Text("Stops")
                .font(.caption)
                .foregroundStyle(.secondary)
            Spacer()
            Button {
                addGradientStop()
            } label: {
                Image(systemName: "plus")
            }
            .buttonStyle(.borderless)
            .disabled(!isEditable)
        }

        let stopCount = core.styleGradientStopCount(layerID: layerID, index: styleIndex)
        ForEach(0 ..< stopCount, id: \.self) { stopIndex in
            gradientStopRow(stopIndex: stopIndex, stopCount: stopCount)
        }
    }

    private var resolvedGradientType: MS_GRADIENT_TYPE {
        let type = core.styleGradientType(layerID: layerID, index: styleIndex)
        return type == .INVALID ? .LINEAR : type
    }

    private var gradientTypeBinding: Binding<MS_GRADIENT_TYPE> {
        Binding {
            resolvedGradientType
        } set: { newValue in
            guard isEditable, newValue != .INVALID else { return }
            perform("Set \(actionPrefix) Gradient Type") {
                _ = core.setGradientType(layerID: layerID, index: styleIndex, type: newValue)
            }
        }
    }

    @ViewBuilder
    private func gradientStopRow(stopIndex: Int, stopCount: Int) -> some View {
        let colorPath = StyleProperty.gradientStopColor(styleIndex: styleIndex, stopIndex: stopIndex)
        let positionPath = StyleProperty.gradientStopPosition(styleIndex: styleIndex,
                                                              stopIndex: stopIndex)
        let colorHasKeyframe = core.keyframeFrames(entityID: layerID, path: colorPath)
            .contains(playheadFrame)
        let positionHasKeyframe = core.keyframeFrames(entityID: layerID, path: positionPath)
            .contains(playheadFrame)

        let stopColor = core.evaluateColor(entityID: layerID, path: colorPath, frame: playheadFrame)
        VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 8) {
                ColorPicker("Stop \(stopIndex + 1)",
                            selection: colorBinding(path: colorPath, hasKeyframe: colorHasKeyframe,
                                                    animatable: true),
                            supportsOpacity: true)
                    .font(.callout)
                    // ColorPicker ignores external Binding updates; remount when model color changes.
                    .id("style-gradient-stop-\(styleIndex)-\(stopIndex)-\(stopColor.r)-\(stopColor.g)-\(stopColor.b)-\(stopColor.a)")
                Button {
                    toggleColorKeyframe(path: colorPath, value: stopColor, hasKeyframe: colorHasKeyframe)
                } label: {
                    Image(systemName: colorHasKeyframe ? "diamond.fill" : "diamond")
                        .foregroundStyle(colorHasKeyframe ? .yellow : .secondary)
                }
                .buttonStyle(.plain)
                .disabled(!isEditable)
                Button {
                    removeGradientStop(stopIndex: stopIndex)
                } label: {
                    Image(systemName: "minus")
                }
                .buttonStyle(.borderless)
                .disabled(!isEditable || stopCount <= 2)
            }
            NumberPropertyRow(
                label: "Pos",
                value: core.evaluateFloat(entityID: layerID, path: positionPath, frame: playheadFrame),
                hasKeyframeAtPlayhead: positionHasKeyframe,
                isEditable: isEditable,
                showsKeyframeButton: true,
                onCommit: { value in
                    writeFloat(path: positionPath, value: value, hasKeyframe: positionHasKeyframe)
                },
                onToggleKeyframe: { value in
                    toggleFloatKeyframe(path: positionPath, value: value,
                                        hasKeyframe: positionHasKeyframe)
                },
            )
        }
    }

    private func vec2PropertyRow(label: String, path: String) -> some View {
        let value = core.evaluateVec2(entityID: layerID, path: path, frame: playheadFrame)
        let hasKeyframe = core.keyframeFrames(entityID: layerID, path: path).contains(playheadFrame)
        return compactVectorRow(name: label, animatable: true, hasKeyframe: hasKeyframe,
                                onToggleKeyframe: {
                                    toggleVec2Keyframe(path: path, value: value,
                                                       hasKeyframe: hasKeyframe)
                                }) {
            CompactAxisField(label: "X", value: Float(value.dx), isEditable: isEditable) { x in
                writeVec2(path: path, value: CGVector(dx: CGFloat(x), dy: value.dy),
                          hasKeyframe: hasKeyframe)
            }
            CompactAxisField(label: "Y", value: Float(value.dy), isEditable: isEditable) { y in
                writeVec2(path: path, value: CGVector(dx: value.dx, dy: CGFloat(y)),
                          hasKeyframe: hasKeyframe)
            }
        }
    }

    private func floatPropertyRow(label: String, path: String) -> some View {
        let hasKeyframe = core.keyframeFrames(entityID: layerID, path: path).contains(playheadFrame)
        return NumberPropertyRow(
            label: label,
            value: core.evaluateFloat(entityID: layerID, path: path, frame: playheadFrame),
            hasKeyframeAtPlayhead: hasKeyframe,
            isEditable: isEditable,
            showsKeyframeButton: true,
            onCommit: { value in
                writeFloat(path: path, value: value, hasKeyframe: hasKeyframe)
            },
            onToggleKeyframe: { value in
                toggleFloatKeyframe(path: path, value: value, hasKeyframe: hasKeyframe)
            },
        )
    }

    private func addGradientStop() {
        guard isEditable else { return }
        let stopCount = core.styleGradientStopCount(layerID: layerID, index: styleIndex)
        let insertIndex = max(stopCount - 1, 0)
        perform("Add \(actionPrefix) Gradient Stop") {
            _ = core.addGradientStop(layerID: layerID, index: styleIndex, insertIndex: insertIndex,
                                     color: MotionColor(r: 0.5, g: 0.5, b: 0.5, a: 1),
                                     position: 0.5)
        }
    }

    private func removeGradientStop(stopIndex: Int) {
        guard isEditable else { return }
        perform("Remove \(actionPrefix) Gradient Stop") {
            _ = core.removeGradientStop(layerID: layerID, index: styleIndex, stopIndex: stopIndex)
        }
    }

    private var editingShaderItem: Binding<ShaderIDItem?> {
        Binding {
            editingShaderID.map(ShaderIDItem.init)
        } set: { newValue in
            editingShaderID = newValue?.id
        }
    }

    @ViewBuilder
    private func uniformRow(shaderID: UInt64, uniformIndex: Int) -> some View {
        let name = core.shaderUniformName(shaderID, index: uniformIndex)
        let format = core.shaderUniformFormat(shaderID, index: uniformIndex)
        let animatable = core.shaderUniformAnimatable(shaderID, index: uniformIndex)
        let path = StyleProperty.uniformValue(name, styleIndex: styleIndex)
        let hasKeyframe = animatable
            && core.keyframeFrames(entityID: layerID, path: path).contains(playheadFrame)

        switch format {
        case .FLOAT:
            NumberPropertyRow(
                label: name,
                value: core.evaluateFloat(entityID: layerID, path: path, frame: playheadFrame),
                hasKeyframeAtPlayhead: hasKeyframe,
                isEditable: isEditable,
                showsKeyframeButton: animatable,
                onCommit: { value in
                    writeFloat(path: path, value: value, hasKeyframe: hasKeyframe)
                },
                onToggleKeyframe: { value in
                    toggleFloatKeyframe(path: path, value: value, hasKeyframe: hasKeyframe)
                },
            )
        case .FLOAT2:
            let value = core.evaluateVec2(entityID: layerID, path: path, frame: playheadFrame)
            compactVectorRow(name: name, animatable: animatable, hasKeyframe: hasKeyframe,
                             onToggleKeyframe: {
                                 toggleVec2Keyframe(path: path, value: value, hasKeyframe: hasKeyframe)
                             }) {
                CompactAxisField(label: "X", value: Float(value.dx), isEditable: isEditable) { x in
                    writeVec2(path: path, value: CGVector(dx: CGFloat(x), dy: value.dy),
                              hasKeyframe: hasKeyframe)
                }
                CompactAxisField(label: "Y", value: Float(value.dy), isEditable: isEditable) { y in
                    writeVec2(path: path, value: CGVector(dx: value.dx, dy: CGFloat(y)),
                              hasKeyframe: hasKeyframe)
                }
            }
        case .FLOAT3:
            let value = core.evaluateVec3(entityID: layerID, path: path, frame: playheadFrame)
            compactVectorRow(name: name, animatable: animatable, hasKeyframe: hasKeyframe,
                             onToggleKeyframe: {
                                 toggleVec3Keyframe(path: path, value: value, hasKeyframe: hasKeyframe)
                             }) {
                CompactAxisField(label: "X", value: value.x, isEditable: isEditable) { x in
                    writeVec3(path: path, value: SIMD3(x, value.y, value.z), hasKeyframe: hasKeyframe)
                }
                CompactAxisField(label: "Y", value: value.y, isEditable: isEditable) { y in
                    writeVec3(path: path, value: SIMD3(value.x, y, value.z), hasKeyframe: hasKeyframe)
                }
                CompactAxisField(label: "Z", value: value.z, isEditable: isEditable) { z in
                    writeVec3(path: path, value: SIMD3(value.x, value.y, z), hasKeyframe: hasKeyframe)
                }
            }
        case .FLOAT4:
            let value = core.evaluateVec4(entityID: layerID, path: path, frame: playheadFrame)
            compactVectorRow(name: name, animatable: animatable, hasKeyframe: hasKeyframe,
                             onToggleKeyframe: {
                                 toggleVec4Keyframe(path: path, value: value, hasKeyframe: hasKeyframe)
                             }) {
                CompactAxisField(label: "X", value: value.x, isEditable: isEditable) { x in
                    writeVec4(path: path, value: SIMD4(x, value.y, value.z, value.w),
                              hasKeyframe: hasKeyframe)
                }
                CompactAxisField(label: "Y", value: value.y, isEditable: isEditable) { y in
                    writeVec4(path: path, value: SIMD4(value.x, y, value.z, value.w),
                              hasKeyframe: hasKeyframe)
                }
                CompactAxisField(label: "Z", value: value.z, isEditable: isEditable) { z in
                    writeVec4(path: path, value: SIMD4(value.x, value.y, z, value.w),
                              hasKeyframe: hasKeyframe)
                }
                CompactAxisField(label: "W", value: value.w, isEditable: isEditable) { w in
                    writeVec4(path: path, value: SIMD4(value.x, value.y, value.z, w),
                              hasKeyframe: hasKeyframe)
                }
            }
        case .COLOR:
            HStack(spacing: 8) {
                ColorPicker(name,
                            selection: colorBinding(path: path, hasKeyframe: hasKeyframe,
                                                    animatable: animatable),
                            supportsOpacity: true)
                    .font(.callout)
                if animatable {
                    Button {
                        let value = core.evaluateColor(entityID: layerID, path: path, frame: playheadFrame)
                        toggleColorKeyframe(path: path, value: value, hasKeyframe: hasKeyframe)
                    } label: {
                        Image(systemName: hasKeyframe ? "diamond.fill" : "diamond")
                            .foregroundStyle(hasKeyframe ? .yellow : .secondary)
                    }
                    .buttonStyle(.plain)
                    .disabled(!isEditable)
                }
            }
        default:
            EmptyView()
        }
    }

    private func compactVectorRow(
        name: String,
        animatable: Bool,
        hasKeyframe: Bool,
        onToggleKeyframe: @escaping () -> Void,
        @ViewBuilder fields: () -> some View,
    ) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack {
                Text(name)
                    .font(.callout)
                Spacer(minLength: 0)
                if animatable {
                    Button(action: onToggleKeyframe) {
                        Image(systemName: hasKeyframe ? "diamond.fill" : "diamond")
                            .foregroundStyle(hasKeyframe ? .yellow : .secondary)
                    }
                    .buttonStyle(.plain)
                    .disabled(!isEditable)
                }
            }
            HStack(spacing: 6) {
                fields()
            }
        }
    }

    private func colorBinding(path: String, hasKeyframe: Bool, animatable: Bool) -> Binding<Color> {
        Binding {
            core.evaluateColor(entityID: layerID, path: path, frame: playheadFrame).swiftUIColor
        } set: { newValue in
            guard isEditable else { return }
            let value = MotionColor(newValue).clampedChannels()
            perform("Set \(actionPrefix) Uniform") {
                if animatable, hasKeyframe {
                    core.addKeyframeColor(entityID: layerID, path: path, frame: playheadFrame, value: value)
                } else {
                    core.setStaticColor(entityID: layerID, path: path, value: value)
                }
                core.endMergeGroup()
            }
        }
    }

    private func writeFloat(path: String, value: Float, hasKeyframe: Bool) {
        guard isEditable else { return }
        perform("Set \(actionPrefix) Uniform") {
            if hasKeyframe {
                core.addKeyframeFloat(entityID: layerID, path: path, frame: playheadFrame, value: value)
            } else {
                core.setStaticFloat(entityID: layerID, path: path, value: value)
            }
            core.endMergeGroup()
        }
    }

    private func writeVec2(path: String, value: CGVector, hasKeyframe: Bool) {
        guard isEditable else { return }
        perform("Set \(actionPrefix) Uniform") {
            if hasKeyframe {
                core.addKeyframeVec2(entityID: layerID, path: path, frame: playheadFrame, value: value)
            } else {
                core.setStaticVec2(entityID: layerID, path: path, value: value)
            }
            core.endMergeGroup()
        }
    }

    private func writeVec3(path: String, value: SIMD3<Float>, hasKeyframe: Bool) {
        guard isEditable else { return }
        perform("Set \(actionPrefix) Uniform") {
            if hasKeyframe {
                core.addKeyframeVec3(entityID: layerID, path: path, frame: playheadFrame, value: value)
            } else {
                core.setStaticVec3(entityID: layerID, path: path, value: value)
            }
            core.endMergeGroup()
        }
    }

    private func writeVec4(path: String, value: SIMD4<Float>, hasKeyframe: Bool) {
        guard isEditable else { return }
        perform("Set \(actionPrefix) Uniform") {
            if hasKeyframe {
                core.addKeyframeVec4(entityID: layerID, path: path, frame: playheadFrame, value: value)
            } else {
                core.setStaticVec4(entityID: layerID, path: path, value: value)
            }
            core.endMergeGroup()
        }
    }

    private func toggleFloatKeyframe(path: String, value: Float, hasKeyframe: Bool) {
        guard isEditable else { return }
        if hasKeyframe {
            perform("Delete Keyframe") {
                core.removeKeyframe(entityID: layerID, path: path, frame: playheadFrame)
            }
        } else {
            perform("Add Keyframe") {
                core.addKeyframeFloat(entityID: layerID, path: path, frame: playheadFrame, value: value)
            }
        }
    }

    private func toggleVec2Keyframe(path: String, value: CGVector, hasKeyframe: Bool) {
        guard isEditable else { return }
        if hasKeyframe {
            perform("Delete Keyframe") {
                core.removeKeyframe(entityID: layerID, path: path, frame: playheadFrame)
            }
        } else {
            perform("Add Keyframe") {
                core.addKeyframeVec2(entityID: layerID, path: path, frame: playheadFrame, value: value)
            }
        }
    }

    private func toggleVec3Keyframe(path: String, value: SIMD3<Float>, hasKeyframe: Bool) {
        guard isEditable else { return }
        if hasKeyframe {
            perform("Delete Keyframe") {
                core.removeKeyframe(entityID: layerID, path: path, frame: playheadFrame)
            }
        } else {
            perform("Add Keyframe") {
                core.addKeyframeVec3(entityID: layerID, path: path, frame: playheadFrame, value: value)
            }
        }
    }

    private func toggleVec4Keyframe(path: String, value: SIMD4<Float>, hasKeyframe: Bool) {
        guard isEditable else { return }
        if hasKeyframe {
            perform("Delete Keyframe") {
                core.removeKeyframe(entityID: layerID, path: path, frame: playheadFrame)
            }
        } else {
            perform("Add Keyframe") {
                core.addKeyframeVec4(entityID: layerID, path: path, frame: playheadFrame, value: value)
            }
        }
    }

    private func toggleColorKeyframe(path: String, value: MotionColor, hasKeyframe: Bool) {
        guard isEditable else { return }
        if hasKeyframe {
            perform("Delete Keyframe") {
                core.removeKeyframe(entityID: layerID, path: path, frame: playheadFrame)
            }
        } else {
            perform("Add Keyframe") {
                core.addKeyframeColor(entityID: layerID, path: path, frame: playheadFrame, value: value)
            }
        }
    }
}

/// Short-label numeric field for vector axes in the narrow inspector (avoids nesting
/// `NumberPropertyRow`, whose 78pt label squeezes out the text field).
private struct CompactAxisField: View {
    let label: String
    let value: Float
    let isEditable: Bool
    let onCommit: (Float) -> Void

    @State private var draft = ""
    @FocusState private var isFieldFocused: Bool

    var body: some View {
        HStack(spacing: 3) {
            Text(label)
                .font(.caption2)
                .foregroundStyle(.secondary)
                .frame(width: 12, alignment: .leading)
            TextField("", text: $draft)
                .textFieldStyle(.plain)
                .font(.caption)
                .padding(.horizontal, 4)
                .padding(.vertical, 3)
                .background(Color(.secondarySystemBackground), in: RoundedRectangle(cornerRadius: 4))
                .overlay(
                    RoundedRectangle(cornerRadius: 4)
                        .stroke(Color.secondary.opacity(isEditable ? 0.4 : 0.18)),
                )
                .onSubmit(commitDraft)
                .disabled(!isEditable)
                .focused($isFieldFocused)
                .onChange(of: isFieldFocused) { _, focused in
                    if !focused {
                        commitDraft()
                    }
                }
        }
        .onChange(of: value, initial: true) { _, newValue in
            // Always mirror model value (including undo/redo while focused), matching
            // NumberPropertyRow — otherwise Start/End stay stale after Cmd+Z.
            draft = formattedValue(newValue)
        }
    }

    private func commitDraft() {
        guard isEditable else {
            draft = formattedValue(value)
            return
        }
        let trimmed = draft.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let committed = Float(trimmed), committed.isFinite else {
            draft = formattedValue(value)
            return
        }
        draft = formattedValue(committed)
        if committed != value {
            onCommit(committed)
        }
    }

    private func formattedValue(_ value: Float) -> String {
        var formatted = String(format: "%.2f", locale: Locale(identifier: "en_US_POSIX"), Double(value))
        while formatted.last == "0" {
            formatted.removeLast()
        }
        if formatted.last == "." {
            formatted.removeLast()
        }
        return formatted
    }
}

private struct ShaderIDItem: Identifiable {
    let id: UInt64
}
