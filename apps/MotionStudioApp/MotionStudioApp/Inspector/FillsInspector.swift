//
//  FillsInspector.swift
//  MotionStudioApp
//
//  Fill style list editor for shape layers.
//

import MotionStudioBridging
import SwiftUI

/// One row per fill (color with keyframe toggle, blend mode, delete) plus an
/// add button in the section header. Fills are addressed by their index in the
/// layer's style list, matching the "styles[N]" property paths.
struct FillsInspector: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    @Environment(PlayheadClock.self) private var clock
    let isEditable: Bool

    private var playheadFrame: Int64 {
        clock.frame
    }

    let perform: (String, () -> Void) -> Void

    var body: some View {
        // Re-render on any document mutation; bridge reads don't trigger observation.
        let _ = core.revision
        let fills = fillIndices()
        HStack {
            Text("Fills")
                .font(.subheadline)
                .foregroundStyle(.secondary)
            Spacer()
            Button {
                addFill()
            } label: {
                Image(systemName: "plus")
            }
            .disabled(!isEditable)
        }
        ForEach(Array(fills.enumerated()), id: \.element) { position, styleIndex in
            let hasKeyframe = hasKeyframe(styleIndex: styleIndex)
            HStack(spacing: 8) {
                ColorPicker("Fill \(position + 1)",
                            selection: colorBinding(styleIndex: styleIndex, hasKeyframe: hasKeyframe),
                            supportsOpacity: true)
                    .font(.callout)
                Picker("", selection: blendBinding(styleIndex: styleIndex)) {
                    ForEach(MS_BLEND.allCases) { mode in
                        Text(mode.label).tag(mode)
                    }
                }
                .labelsHidden()
                .pickerStyle(.menu)
                .fixedSize()
                Button {
                    toggleKeyframe(styleIndex: styleIndex, hasKeyframe: hasKeyframe)
                } label: {
                    Image(systemName: hasKeyframe ? "diamond.fill" : "diamond")
                        .foregroundStyle(hasKeyframe ? .yellow : .secondary)
                }
                .buttonStyle(.plain)
                .help(hasKeyframe ? "Delete keyframe at playhead" : "Add keyframe at playhead")
                Button(role: .destructive) {
                    removeFill(styleIndex: styleIndex)
                } label: {
                    Image(systemName: "minus")
                }
            }
            .disabled(!isEditable)
        }
    }

    private func fillIndices() -> [Int] {
        (0 ..< core.styleCount(layerID: layerID)).filter { index in
            core.styleType(layerID: layerID, index: index) == .FILL
        }
    }

    private func fillColorPath(styleIndex: Int) -> String {
        StyleProperty.color.path(at: styleIndex)
    }

    private func hasKeyframe(styleIndex: Int) -> Bool {
        core.keyframeFrames(entityID: layerID, path: fillColorPath(styleIndex: styleIndex))
            .contains(playheadFrame)
    }

    private func colorBinding(styleIndex: Int, hasKeyframe: Bool) -> Binding<Color> {
        Binding {
            core.evaluateColor(entityID: layerID, path: fillColorPath(styleIndex: styleIndex),
                               frame: playheadFrame).swiftUIColor
        } set: { newValue in
            guard isEditable else { return }
            let path = fillColorPath(styleIndex: styleIndex)
            let value = MotionColor(newValue).clampedChannels()
            perform("Set Fill Color") {
                if hasKeyframe {
                    core.addKeyframeColor(entityID: layerID, path: path, frame: playheadFrame, value: value)
                } else {
                    core.setStaticColor(entityID: layerID, path: path, value: value)
                }
                core.endDrag()
            }
        }
    }

    private func blendBinding(styleIndex: Int) -> Binding<MS_BLEND> {
        Binding {
            let mode = core.styleBlendMode(layerID: layerID, index: styleIndex)
            return mode == .INVALID ? .NORMAL : mode
        } set: { newValue in
            guard isEditable else { return }
            perform("Set Fill Blend Mode") {
                core.setStyleBlendMode(layerID: layerID, index: styleIndex, blendMode: newValue)
            }
        }
    }

    private func toggleKeyframe(styleIndex: Int, hasKeyframe: Bool) {
        guard isEditable else { return }
        let path = fillColorPath(styleIndex: styleIndex)
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

    private func addFill() {
        guard isEditable else { return }
        perform("Add Fill") {
            core.addFillStyle(layerID: layerID)
        }
    }

    private func removeFill(styleIndex: Int) {
        guard isEditable else { return }
        perform("Remove Fill") {
            core.removeStyle(layerID: layerID, index: styleIndex)
        }
    }
}
