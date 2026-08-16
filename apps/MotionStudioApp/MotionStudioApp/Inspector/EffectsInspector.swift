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
