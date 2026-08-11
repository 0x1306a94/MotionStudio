//
//  FillsInspector.swift
//  MotionStudioApp
//
//  Fill style list editor for shape layers.
//

import MotionStudioBridging
import SwiftUI

/// One row per fill (paint mode / color or shader uniforms, blend, keyframe,
/// delete) plus an add button in the section header. Fills are addressed by
/// their index in the layer's style list, matching the "styles[N]" property paths.
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
        // Newest / topmost paint first (matches Figma: list top = draw top).
        let fills = Array(fillIndices().reversed())
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
            let paintMode = resolvedPaintMode(styleIndex: styleIndex)
            let hasKeyframe = hasKeyframe(styleIndex: styleIndex)
            VStack(alignment: .leading, spacing: 6) {
                StyleShaderPaintControls(core: core, layerID: layerID, styleIndex: styleIndex,
                                         playheadFrame: playheadFrame, isEditable: isEditable,
                                         actionPrefix: "Fill", perform: perform)
                HStack(spacing: 8) {
                    if paintMode == .COLOR {
                        ColorPicker("Fill \(position + 1)",
                                    selection: colorBinding(styleIndex: styleIndex, hasKeyframe: hasKeyframe),
                                    supportsOpacity: true)
                            .font(.callout)
                        Button {
                            toggleKeyframe(styleIndex: styleIndex, hasKeyframe: hasKeyframe)
                        } label: {
                            Image(systemName: hasKeyframe ? "diamond.fill" : "diamond")
                                .foregroundStyle(hasKeyframe ? .yellow : .secondary)
                        }
                        .buttonStyle(.plain)
                        .help(hasKeyframe ? "Delete keyframe at playhead" : "Add keyframe at playhead")
                    } else {
                        Text("Fill \(position + 1)")
                            .font(.callout)
                            .foregroundStyle(.secondary)
                    }
                    Picker("", selection: blendBinding(styleIndex: styleIndex)) {
                        ForEach(MS_BLEND.allCases) { mode in
                            Text(mode.label).tag(mode)
                        }
                    }
                    .labelsHidden()
                    .pickerStyle(.menu)
                    .fixedSize()
                    Button {
                        moveFill(styleIndex: styleIndex, visuallyUp: true)
                    } label: {
                        Image(systemName: "chevron.up")
                    }
                    .disabled(!isEditable || !canMoveFill(styleIndex: styleIndex, visuallyUp: true))
                    .help("Bring fill forward")
                    Button {
                        moveFill(styleIndex: styleIndex, visuallyUp: false)
                    } label: {
                        Image(systemName: "chevron.down")
                    }
                    .disabled(!isEditable || !canMoveFill(styleIndex: styleIndex, visuallyUp: false))
                    .help("Send fill backward")
                    Button(role: .destructive) {
                        removeFill(styleIndex: styleIndex)
                    } label: {
                        Image(systemName: "minus")
                    }
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

    private func resolvedPaintMode(styleIndex: Int) -> MS_PAINT_MODE {
        let mode = core.stylePaintMode(layerID: layerID, index: styleIndex)
        return mode == .INVALID ? .COLOR : mode
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

    private func canMoveFill(styleIndex: Int, visuallyUp: Bool) -> Bool {
        let ascending = fillIndices()
        guard let pos = ascending.firstIndex(of: styleIndex) else { return false }
        if visuallyUp {
            return pos + 1 < ascending.count
        }
        return pos > 0
    }

    private func moveFill(styleIndex: Int, visuallyUp: Bool) {
        guard isEditable else { return }
        let ascending = fillIndices()
        guard let pos = ascending.firstIndex(of: styleIndex) else { return }
        let neighborPos = visuallyUp ? pos + 1 : pos - 1
        guard ascending.indices.contains(neighborPos) else { return }
        let toIndex = ascending[neighborPos]
        perform("Move Fill") {
            core.moveStyle(layerID: layerID, from: styleIndex, to: toIndex)
        }
    }
}
