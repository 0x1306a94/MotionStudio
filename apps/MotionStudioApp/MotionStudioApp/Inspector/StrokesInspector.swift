//
//  StrokesInspector.swift
//  MotionStudioApp
//
//  Stroke style list editor for shape layers.
//

import MotionStudioBridging
import SwiftUI

/// One card per stroke: color/blend/position row plus width and trim rows,
/// every numeric property with a keyframe toggle. Strokes are addressed by
/// their index in the layer's style list, matching the "styles[N]" paths.
struct StrokesInspector: View {
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
        let strokes = strokeIndices()
        let isTextLayer = core.layerType(layerID) == .TEXT
        HStack {
            Text("Strokes")
                .font(.subheadline)
                .foregroundStyle(.secondary)
            Spacer()
            Button {
                addStroke()
            } label: {
                Image(systemName: "plus")
            }
            .disabled(!isEditable)
        }
        ForEach(Array(strokes.enumerated()), id: \.element) { position, styleIndex in
            let hasColorKeyframe = hasKeyframe(styleIndex: styleIndex, property: .color)
            VStack(alignment: .leading, spacing: 6) {
                HStack(spacing: 8) {
                    ColorPicker("Stroke \(position + 1)",
                                selection: colorBinding(styleIndex: styleIndex, hasKeyframe: hasColorKeyframe),
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
                        toggleColorKeyframe(styleIndex: styleIndex, hasKeyframe: hasColorKeyframe)
                    } label: {
                        Image(systemName: hasColorKeyframe ? "diamond.fill" : "diamond")
                            .foregroundStyle(hasColorKeyframe ? .yellow : .secondary)
                    }
                    .buttonStyle(.plain)
                    .help(hasColorKeyframe ? "Delete keyframe at playhead" : "Add keyframe at playhead")
                    Button(role: .destructive) {
                        removeStroke(styleIndex: styleIndex)
                    } label: {
                        Image(systemName: "minus")
                    }
                }
                if !isTextLayer {
                    HStack(spacing: 6) {
                        Text("Position")
                            .font(.callout)
                            .frame(width: 78, alignment: .leading)
                        Picker("", selection: positionBinding(styleIndex: styleIndex)) {
                            ForEach(MS_STROKE_POSITION.allCases) { tag in
                                Text(tag.label).tag(tag)
                            }
                        }
                        .labelsHidden()
                        .pickerStyle(.segmented)
                    }
                }
                floatRow(styleIndex: styleIndex, label: "Width", property: .width)
                if !isTextLayer {
                    floatRow(styleIndex: styleIndex, label: "Trim Start", property: .trimStart)
                    floatRow(styleIndex: styleIndex, label: "Trim End", property: .trimEnd)
                    floatRow(styleIndex: styleIndex, label: "Trim Offset", property: .trimOffset)
                }
            }
            .disabled(!isEditable)
        }
    }

    private func strokeIndices() -> [Int] {
        (0 ..< core.styleCount(layerID: layerID)).filter { index in
            core.styleType(layerID: layerID, index: index) == .STROKE
        }
    }

    private func stylePath(styleIndex: Int, property: StyleProperty) -> String {
        property.path(at: styleIndex)
    }

    private func hasKeyframe(styleIndex: Int, property: StyleProperty) -> Bool {
        core.keyframeFrames(entityID: layerID,
                            path: stylePath(styleIndex: styleIndex, property: property))
            .contains(playheadFrame)
    }

    private func floatRow(styleIndex: Int, label: String, property: StyleProperty) -> some View {
        let path = stylePath(styleIndex: styleIndex, property: property)
        let hasKeyframe = core.keyframeFrames(entityID: layerID, path: path)
            .contains(playheadFrame)
        return NumberPropertyRow(label: label,
                                 value: core.evaluateFloat(entityID: layerID, path: path, frame: playheadFrame),
                                 hasKeyframeAtPlayhead: hasKeyframe,
                                 isEditable: isEditable)
        { newValue in
            guard isEditable else { return }
            perform("Set Stroke \(label)") {
                if hasKeyframe {
                    core.addKeyframeFloat(entityID: layerID, path: path, frame: playheadFrame, value: newValue)
                } else {
                    core.setStaticFloat(entityID: layerID, path: path, value: newValue)
                }
                core.endMergeGroup()
            }
        } onToggleKeyframe: { value in
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
    }

    private func colorBinding(styleIndex: Int, hasKeyframe: Bool) -> Binding<Color> {
        Binding {
            core.evaluateColor(entityID: layerID, path: stylePath(styleIndex: styleIndex, property: .color),
                               frame: playheadFrame).swiftUIColor
        } set: { newValue in
            guard isEditable else { return }
            let path = stylePath(styleIndex: styleIndex, property: .color)
            let value = MotionColor(newValue).clampedChannels()
            perform("Set Stroke Color") {
                if hasKeyframe {
                    core.addKeyframeColor(entityID: layerID, path: path, frame: playheadFrame, value: value)
                } else {
                    core.setStaticColor(entityID: layerID, path: path, value: value)
                }
                core.endMergeGroup()
            }
        }
    }

    private func blendBinding(styleIndex: Int) -> Binding<MS_BLEND> {
        Binding {
            let mode = core.styleBlendMode(layerID: layerID, index: styleIndex)
            return mode == .INVALID ? .NORMAL : mode
        } set: { newValue in
            guard isEditable else { return }
            perform("Set Stroke Blend Mode") {
                core.setStyleBlendMode(layerID: layerID, index: styleIndex, blendMode: newValue)
            }
        }
    }

    private func positionBinding(styleIndex: Int) -> Binding<MS_STROKE_POSITION> {
        Binding {
            let position = core.strokePosition(layerID: layerID, index: styleIndex)
            return position == .INVALID ? .CENTER : position
        } set: { newValue in
            guard isEditable else { return }
            perform("Set Stroke Position") {
                core.setStrokePosition(layerID: layerID, index: styleIndex, position: newValue)
            }
        }
    }

    private func toggleColorKeyframe(styleIndex: Int, hasKeyframe: Bool) {
        guard isEditable else { return }
        let path = stylePath(styleIndex: styleIndex, property: .color)
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

    private func addStroke() {
        guard isEditable else { return }
        perform("Add Stroke") {
            core.addStrokeStyle(layerID: layerID)
        }
    }

    private func removeStroke(styleIndex: Int) {
        guard isEditable else { return }
        perform("Remove Stroke") {
            core.removeStyle(layerID: layerID, index: styleIndex)
        }
    }
}
