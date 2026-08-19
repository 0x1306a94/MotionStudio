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
        let _ = core.panelRevision
        // Newest / topmost paint first (matches Figma: list top = draw top).
        let strokes = Array(strokeIndices().reversed())
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
            let paintMode = resolvedPaintMode(styleIndex: styleIndex)
            VStack(alignment: .leading, spacing: 6) {
                StyleShaderPaintControls(core: core, layerID: layerID, styleIndex: styleIndex,
                                         playheadFrame: playheadFrame, isEditable: isEditable,
                                         actionPrefix: "Stroke", perform: perform)
                {
                    HStack(spacing: 8) {
                        Text("Stroke \(position + 1)")
                            .font(.callout)
                            .foregroundStyle(.secondary)
                        Spacer(minLength: 0)
                        Picker("", selection: blendBinding(styleIndex: styleIndex)) {
                            ForEach(MS_BLEND.allCases) { mode in
                                Text(mode.label).tag(mode)
                            }
                        }
                        .labelsHidden()
                        .pickerStyle(.menu)
                        .fixedSize()
                        Button {
                            moveStroke(styleIndex: styleIndex, visuallyUp: true)
                        } label: {
                            Image(systemName: "chevron.up")
                        }
                        .disabled(!isEditable || !canMoveStroke(styleIndex: styleIndex, visuallyUp: true))
                        .help("Bring stroke forward")
                        Button {
                            moveStroke(styleIndex: styleIndex, visuallyUp: false)
                        } label: {
                            Image(systemName: "chevron.down")
                        }
                        .disabled(!isEditable || !canMoveStroke(styleIndex: styleIndex, visuallyUp: false))
                        .help("Send stroke backward")
                        Button(role: .destructive) {
                            removeStroke(styleIndex: styleIndex)
                        } label: {
                            Image(systemName: "minus")
                        }
                    }
                }
                if paintMode == .COLOR {
                    HStack(spacing: 8) {
                        ColorPicker("Stroke \(position + 1)",
                                    selection: colorBinding(styleIndex: styleIndex, hasKeyframe: hasColorKeyframe),
                                    supportsOpacity: true)
                            .labelsHidden()
                            .fixedSize()
                        Button {
                            toggleColorKeyframe(styleIndex: styleIndex, hasKeyframe: hasColorKeyframe)
                        } label: {
                            Image(systemName: hasColorKeyframe ? "diamond.fill" : "diamond")
                                .foregroundStyle(hasColorKeyframe ? .yellow : .secondary)
                        }
                        .buttonStyle(.plain)
                        .help(hasColorKeyframe ? "Delete keyframe at playhead" : "Add keyframe at playhead")
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
                    HStack(spacing: 6) {
                        Text("Cap")
                            .font(.callout)
                            .frame(width: 78, alignment: .leading)
                        Picker("", selection: capBinding(styleIndex: styleIndex)) {
                            ForEach(MS_LINE_CAP.allCases) { tag in
                                Text(tag.label).tag(tag)
                            }
                        }
                        .labelsHidden()
                        .pickerStyle(.segmented)
                    }
                    HStack(spacing: 6) {
                        Text("Join")
                            .font(.callout)
                            .frame(width: 78, alignment: .leading)
                        Picker("", selection: joinBinding(styleIndex: styleIndex)) {
                            ForEach(MS_LINE_JOIN.allCases) { tag in
                                Text(tag.label).tag(tag)
                            }
                        }
                        .labelsHidden()
                        .pickerStyle(.segmented)
                    }
                    if joinValue(styleIndex: styleIndex) == .MITER {
                        staticFloatRow(styleIndex: styleIndex, label: "Miter",
                                       value: core.strokeMiterLimit(layerID: layerID, index: styleIndex))
                        { newValue in
                            perform("Set Stroke Miter") {
                                core.setStrokeMiterLimit(layerID: layerID, index: styleIndex,
                                                         miterLimit: newValue)
                            }
                        }
                    }
                    HStack(spacing: 6) {
                        Text("Style")
                            .font(.callout)
                            .frame(width: 78, alignment: .leading)
                        Picker("", selection: strokeModeBinding(styleIndex: styleIndex)) {
                            ForEach(MS_STROKE_MODE.allCases) { tag in
                                Text(tag.label).tag(tag)
                            }
                        }
                        .labelsHidden()
                        .pickerStyle(.segmented)
                    }
                    if strokeModeValue(styleIndex: styleIndex) == .DASHED {
                        dashPatternEditor(styleIndex: styleIndex)
                        floatRow(styleIndex: styleIndex, label: "Offset", property: .dashOffset)
                    }
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

    private func resolvedPaintMode(styleIndex: Int) -> MS_PAINT_MODE {
        let mode = core.stylePaintMode(layerID: layerID, index: styleIndex)
        return mode == .INVALID ? .COLOR : mode
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

    private func capBinding(styleIndex: Int) -> Binding<MS_LINE_CAP> {
        Binding {
            let cap = core.strokeCap(layerID: layerID, index: styleIndex)
            return cap == .INVALID ? .BUTT : cap
        } set: { newValue in
            guard isEditable else { return }
            perform("Set Stroke Cap") {
                core.setStrokeCap(layerID: layerID, index: styleIndex, cap: newValue)
            }
        }
    }

    private func joinBinding(styleIndex: Int) -> Binding<MS_LINE_JOIN> {
        Binding {
            let join = core.strokeJoin(layerID: layerID, index: styleIndex)
            return join == .INVALID ? .MITER : join
        } set: { newValue in
            guard isEditable else { return }
            perform("Set Stroke Join") {
                core.setStrokeJoin(layerID: layerID, index: styleIndex, join: newValue)
            }
        }
    }

    private func joinValue(styleIndex: Int) -> MS_LINE_JOIN {
        let join = core.strokeJoin(layerID: layerID, index: styleIndex)
        return join == .INVALID ? .MITER : join
    }

    private func strokeModeBinding(styleIndex: Int) -> Binding<MS_STROKE_MODE> {
        Binding {
            strokeModeValue(styleIndex: styleIndex)
        } set: { newValue in
            guard isEditable else { return }
            perform("Set Stroke Style") {
                core.setStrokeMode(layerID: layerID, index: styleIndex, strokeMode: newValue)
            }
        }
    }

    private func strokeModeValue(styleIndex: Int) -> MS_STROKE_MODE {
        let mode = core.strokeMode(layerID: layerID, index: styleIndex)
        return mode == .INVALID ? .SOLID : mode
    }

    private func staticFloatRow(styleIndex _: Int, label: String, value: Float,
                                onCommit: @escaping (Float) -> Void) -> some View
    {
        NumberPropertyRow(label: label,
                          value: value,
                          hasKeyframeAtPlayhead: false,
                          isEditable: isEditable,
                          showsKeyframeButton: false,
                          onCommit: { newValue in
                              guard isEditable else { return }
                              onCommit(newValue)
                          },
                          onToggleKeyframe: { _ in })
    }

    private func dashIntervals(styleIndex: Int) -> [Float] {
        var dashes = core.strokeDashes(layerID: layerID, index: styleIndex)
        if dashes.count % 2 == 1 {
            dashes.append(dashes.last ?? 8)
        }
        if dashes.count < 2 {
            dashes = [8, 8]
        }
        return dashes
    }

    @ViewBuilder
    private func dashPatternEditor(styleIndex: Int) -> some View {
        let dashes = dashIntervals(styleIndex: styleIndex)
        let pairCount = dashes.count / 2
        ForEach(0 ..< pairCount, id: \.self) { pairIndex in
            HStack(spacing: 6) {
                staticFloatRow(styleIndex: styleIndex,
                               label: pairIndex == 0 ? "Dash" : "Dash \(pairIndex + 1)",
                               value: dashes[pairIndex * 2])
                { newValue in
                    setDash(styleIndex: styleIndex, intervalIndex: pairIndex * 2, value: newValue)
                }
                if pairCount > 1 {
                    Button(role: .destructive) {
                        removeDashPair(styleIndex: styleIndex, pairIndex: pairIndex)
                    } label: {
                        Image(systemName: "minus")
                    }
                    .disabled(!isEditable)
                }
            }
            staticFloatRow(styleIndex: styleIndex,
                           label: pairIndex == 0 ? "Gap" : "Gap \(pairIndex + 1)",
                           value: dashes[pairIndex * 2 + 1])
            { newValue in
                setDash(styleIndex: styleIndex, intervalIndex: pairIndex * 2 + 1, value: newValue)
            }
        }
        HStack {
            Spacer()
            Button {
                addDashPair(styleIndex: styleIndex)
            } label: {
                Image(systemName: "plus")
            }
            .disabled(!isEditable)
            .help("Add dash/gap pair")
        }
    }

    private func setDash(styleIndex: Int, intervalIndex: Int, value: Float) {
        var dashes = dashIntervals(styleIndex: styleIndex)
        guard intervalIndex >= 0, intervalIndex < dashes.count else { return }
        dashes[intervalIndex] = value
        perform("Set Stroke Dash") {
            core.setStrokeDashes(layerID: layerID, index: styleIndex, dashes: dashes)
        }
    }

    private func addDashPair(styleIndex: Int) {
        var dashes = dashIntervals(styleIndex: styleIndex)
        dashes.append(contentsOf: [8, 8])
        perform("Add Stroke Dash Pair") {
            core.setStrokeDashes(layerID: layerID, index: styleIndex, dashes: dashes)
        }
    }

    private func removeDashPair(styleIndex: Int, pairIndex: Int) {
        var dashes = dashIntervals(styleIndex: styleIndex)
        let start = pairIndex * 2
        guard dashes.count > 2, start + 1 < dashes.count else { return }
        dashes.removeSubrange(start ... start + 1)
        perform("Remove Stroke Dash Pair") {
            core.setStrokeDashes(layerID: layerID, index: styleIndex, dashes: dashes)
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

    private func canMoveStroke(styleIndex: Int, visuallyUp: Bool) -> Bool {
        let ascending = strokeIndices()
        guard let pos = ascending.firstIndex(of: styleIndex) else { return false }
        if visuallyUp {
            return pos + 1 < ascending.count
        }
        return pos > 0
    }

    private func moveStroke(styleIndex: Int, visuallyUp: Bool) {
        guard isEditable else { return }
        let ascending = strokeIndices()
        guard let pos = ascending.firstIndex(of: styleIndex) else { return }
        let neighborPos = visuallyUp ? pos + 1 : pos - 1
        guard ascending.indices.contains(neighborPos) else { return }
        let toIndex = ascending[neighborPos]
        perform("Move Stroke") {
            core.moveStyle(layerID: layerID, from: styleIndex, to: toIndex)
        }
    }
}
