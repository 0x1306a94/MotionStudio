//
//  LayerStylesInspector.swift
//  MotionStudioApp
//
//  Layer style list: add/remove/reorder, enable toggle, blend, and
//  keyframed color / numeric fields. Newest style is listed first.
//

import MotionStudioBridging
import SwiftUI

struct LayerStylesInspector: View {
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
        let styles = Array((0 ..< core.layerFxCount(layerID: layerID)).reversed())
        HStack {
            Text("Layer Styles")
                .font(.subheadline)
                .foregroundStyle(.secondary)
            Spacer()
            Menu {
                ForEach(MS_LAYER_FX.allCases) { type in
                    Button(type.label) {
                        addStyle(type)
                    }
                }
            } label: {
                Image(systemName: "plus")
            }
            .disabled(!isEditable)
        }
        ForEach(styles, id: \.self) { index in
            styleRow(index: index)
                .disabled(!isEditable)
        }
    }

    @ViewBuilder
    private func styleRow(index: Int) -> some View {
        let type = core.layerFxType(layerID: layerID, index: index)
        if type != .INVALID {
            VStack(alignment: .leading, spacing: 6) {
                HStack(spacing: 8) {
                    Text(type.label)
                        .font(.callout)
                    Toggle("", isOn: enabledBinding(index: index))
                        .labelsHidden()
                        .fixedSize()
                    Picker("", selection: blendBinding(index: index)) {
                        ForEach(MS_BLEND.allCases) { mode in
                            Text(mode.label).tag(mode)
                        }
                    }
                    .labelsHidden()
                    .pickerStyle(.menu)
                    .fixedSize()
                    Button {
                        moveStyle(index: index, visuallyUp: true)
                    } label: {
                        Image(systemName: "chevron.up")
                    }
                    .disabled(!isEditable || !canMoveStyle(index: index, visuallyUp: true))
                    .help("Bring style later")
                    Button {
                        moveStyle(index: index, visuallyUp: false)
                    } label: {
                        Image(systemName: "chevron.down")
                    }
                    .disabled(!isEditable || !canMoveStyle(index: index, visuallyUp: false))
                    .help("Bring style earlier")
                    Spacer(minLength: 0)
                    Button(role: .destructive) {
                        removeStyle(index: index)
                    } label: {
                        Image(systemName: "minus")
                    }
                }
                colorRow(index: index)
                switch type {
                case .DROP_SHADOW:
                    floatRow(index: index, property: .opacity, label: "Opacity")
                    floatRow(index: index, property: .angle, label: "Angle")
                    floatRow(index: index, property: .distance, label: "Distance")
                    floatRow(index: index, property: .size, label: "Size")
                    floatRow(index: index, property: .spread, label: "Spread")
                case .OUTER_GLOW:
                    floatRow(index: index, property: .opacity, label: "Opacity")
                    floatRow(index: index, property: .size, label: "Size")
                    floatRow(index: index, property: .spread, label: "Spread")
                    floatRow(index: index, property: .range, label: "Range")
                case .STROKE:
                    floatRow(index: index, property: .opacity, label: "Opacity")
                    floatRow(index: index, property: .size, label: "Size")
                    HStack(spacing: 6) {
                        Text("Position")
                            .font(.callout)
                            .frame(width: 78, alignment: .leading)
                        Picker("", selection: positionBinding(index: index)) {
                            ForEach(MS_STROKE_POSITION.allCases) { tag in
                                Text(tag.label).tag(tag)
                            }
                        }
                        .labelsHidden()
                        .pickerStyle(.segmented)
                    }
                case .INVALID:
                    EmptyView()
                }
            }
            .padding(.vertical, 2)
            .id("layer-fx-row-\(index)-\(core.panelRevision)")
        }
    }

    private func colorRow(index: Int) -> some View {
        let path = LayerStyleFxProperty.color.path(at: index)
        let hasKeyframe = hasKeyframeAtPlayhead(path: path)
        return HStack(spacing: 8) {
            ColorPicker("Color",
                        selection: colorBinding(index: index, hasKeyframe: hasKeyframe),
                        supportsOpacity: false)
                .labelsHidden()
                .fixedSize()
            Button {
                toggleColorKeyframe(index: index, hasKeyframe: hasKeyframe)
            } label: {
                Image(systemName: hasKeyframe ? "diamond.fill" : "diamond")
                    .foregroundStyle(hasKeyframe ? .yellow : .secondary)
            }
            .buttonStyle(.plain)
            .help(hasKeyframe ? "Delete keyframe at playhead" : "Add keyframe at playhead")
        }
        .id("layer-fx-\(path)-\(core.panelRevision)-\(hasKeyframe)")
    }

    private func floatRow(index: Int, property: LayerStyleFxProperty, label: String) -> some View {
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
        .id("layer-fx-\(path)-\(core.panelRevision)-\(hasKeyframe)")
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

    private func colorBinding(index: Int, hasKeyframe: Bool) -> Binding<Color> {
        let path = LayerStyleFxProperty.color.path(at: index)
        return Binding {
            core.evaluateColor(entityID: layerID, path: path, frame: playheadFrame).swiftUIColor
        } set: { newValue in
            guard isEditable else {
                return
            }
            let value = MotionColor(newValue).clampedChannels()
            perform("Set Color") {
                if hasKeyframe {
                    core.addKeyframeColor(entityID: layerID, path: path, frame: playheadFrame, value: value)
                } else {
                    core.setStaticColor(entityID: layerID, path: path, value: value)
                }
                core.endMergeGroup()
            }
        }
    }

    private func toggleColorKeyframe(index: Int, hasKeyframe: Bool) {
        guard isEditable else {
            return
        }
        let path = LayerStyleFxProperty.color.path(at: index)
        if hasKeyframe {
            perform("Delete Keyframe") {
                core.removeKeyframe(entityID: layerID, path: path, frame: playheadFrame)
            }
        } else {
            let value = core.evaluateColor(entityID: layerID, path: path, frame: playheadFrame)
            perform("Add Keyframe") {
                core.addKeyframeColor(entityID: layerID, path: path, frame: playheadFrame, value: value)
            }
        }
    }

    private func enabledBinding(index: Int) -> Binding<Bool> {
        Binding {
            core.layerFxEnabled(layerID: layerID, index: index)
        } set: { newValue in
            guard isEditable else {
                return
            }
            perform("Set Layer Style Enabled") {
                core.setLayerFxEnabled(layerID: layerID, index: index, enabled: newValue)
            }
        }
    }

    private func blendBinding(index: Int) -> Binding<MS_BLEND> {
        Binding {
            core.layerFxBlendMode(layerID: layerID, index: index)
        } set: { newValue in
            guard isEditable else {
                return
            }
            perform("Set Layer Style Blend Mode") {
                core.setLayerFxBlendMode(layerID: layerID, index: index, blendMode: newValue)
            }
        }
    }

    private func positionBinding(index: Int) -> Binding<MS_STROKE_POSITION> {
        Binding {
            let position = core.layerFxStrokePosition(layerID: layerID, index: index)
            return position == .INVALID ? .OUTSIDE : position
        } set: { newValue in
            guard isEditable else {
                return
            }
            perform("Set Layer Style Position") {
                core.setLayerFxStrokePosition(layerID: layerID, index: index, position: newValue)
            }
        }
    }

    private func addStyle(_ type: MS_LAYER_FX) {
        guard isEditable else {
            return
        }
        switch type {
        case .DROP_SHADOW:
            perform("Add Drop Shadow") {
                core.addDropShadowLayerFx(layerID: layerID)
            }
        case .OUTER_GLOW:
            perform("Add Outer Glow") {
                core.addOuterGlowLayerFx(layerID: layerID)
            }
        case .STROKE:
            perform("Add Stroke") {
                core.addStrokeLayerFx(layerID: layerID)
            }
        case .INVALID:
            break
        }
    }

    private func removeStyle(index: Int) {
        guard isEditable else {
            return
        }
        perform("Remove Layer Style") {
            core.removeLayerFx(layerID: layerID, index: index)
        }
    }

    private func canMoveStyle(index: Int, visuallyUp: Bool) -> Bool {
        let count = core.layerFxCount(layerID: layerID)
        guard count > 0, index >= 0, index < count else {
            return false
        }
        if visuallyUp {
            return index + 1 < count
        }
        return index > 0
    }

    private func moveStyle(index: Int, visuallyUp: Bool) {
        guard isEditable else {
            return
        }
        let count = core.layerFxCount(layerID: layerID)
        guard count > 0, index >= 0, index < count else {
            return
        }
        let toIndex = visuallyUp ? index + 1 : index - 1
        guard toIndex >= 0, toIndex < count else {
            return
        }
        perform("Move Layer Style") {
            core.moveLayerFx(layerID: layerID, from: index, to: toIndex)
        }
    }
}
