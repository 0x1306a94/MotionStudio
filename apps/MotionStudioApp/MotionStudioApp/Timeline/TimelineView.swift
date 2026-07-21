//
//  TimelineView.swift
//  MotionStudioApp
//
//  After-Effects-style timeline: a horizontally-frozen left layer stack
//  (name + visibility + lock) and a right time graph with one keyframe row per
//  layer. The body scrolls vertically when layers overflow (layer stack and
//  graph stay in lockstep); the ruler stays pinned above. Horizontal pan/zoom
//  is intentionally deferred, so the left stack is frozen on both axes.
//

import SwiftUI

private let pixelsPerFrame: CGFloat = 6
private let rowHeight: CGFloat = 28
private let layerColumnWidth: CGFloat = 200
private let rulerHeight: CGFloat = 24

struct TimelineView: View {
    let document: MotionDocument
    let editorState: EditorState
    let perform: (String, () -> Void) -> Void
    let registerEdit: (String) -> Void

    var body: some View {
        let core = document.core
        // Drives re-evaluation after every model mutation.
        let _ = core.revision
        let compositionID = core.firstCompositionID
        let duration = core.duration(compositionID: compositionID)
        let frameRate = core.frameRate(compositionID: compositionID)
        let layerIDs = Array(core.layerIDs(compositionID: compositionID).reversed())
        let trackWidth = max(CGFloat(duration) * pixelsPerFrame, 1)
        let totalHeight = max(CGFloat(layerIDs.count) * rowHeight, rowHeight)

        VStack(spacing: 0) {
            TimelineControls(editorState: editorState, duration: duration)
            Divider()
            // Pinned header: layer-column header + ruler (scrubbable).
            HStack(alignment: .top, spacing: 0) {
                LayerColumnHeader()
                    .frame(width: layerColumnWidth, height: rulerHeight)
                RulerCanvas(duration: duration, frameRate: frameRate)
                    .frame(width: trackWidth, height: rulerHeight)
                    .contentShape(Rectangle())
                    .gesture(scrubGesture(duration: duration))
            }
            Divider()
            // Vertically scrolling body: layer stack + graph in lockstep.
            ScrollView(.vertical) {
                HStack(alignment: .top, spacing: 0) {
                    LayerColumn(core: core, layerIDs: layerIDs, editorState: editorState,
                                perform: perform)
                        .frame(width: layerColumnWidth)
                        .frame(minHeight: totalHeight, alignment: .top)
                    ZStack(alignment: .topLeading) {
                        VStack(spacing: 0) {
                            ForEach(layerIDs, id: \.self) { id in
                                TrackRow(core: core,
                                         layerID: id,
                                         property: editorState.timelineProperty,
                                         duration: duration,
                                         perform: perform,
                                         registerEdit: registerEdit)
                                    .frame(height: rowHeight)
                            }
                        }
                        // Playhead spans every row.
                        Rectangle()
                            .fill(.red)
                            .frame(width: 1.5, height: totalHeight)
                            .offset(x: CGFloat(editorState.playheadFrame) * pixelsPerFrame)
                            .allowsHitTesting(false)
                    }
                    .frame(width: trackWidth, height: totalHeight)
                }
            }
        }
    }

    private func scrubGesture(duration: Int64) -> some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { value in
                let frame = Int64((value.location.x / pixelsPerFrame).rounded())
                editorState.playheadFrame = min(max(frame, 0), duration)
            }
    }
}

// MARK: - Layer column

private struct LayerColumnHeader: View {
    var body: some View {
        HStack {
            Text("Layers")
                .font(.caption)
                .foregroundStyle(.secondary)
            Spacer()
        }
        .padding(.horizontal, 8)
    }
}

private struct LayerColumn: View {
    let core: MotionDocumentCore
    let layerIDs: [UInt64]
    let editorState: EditorState
    let perform: (String, () -> Void) -> Void

    var body: some View {
        VStack(spacing: 0) {
            ForEach(layerIDs, id: \.self) { id in
                LayerRow(core: core, layerID: id, editorState: editorState, perform: perform)
                    .frame(height: rowHeight)
            }
        }
    }
}

private struct LayerRow: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    let editorState: EditorState
    let perform: (String, () -> Void) -> Void

    var body: some View {
        let selected = editorState.selectedLayerID == layerID
        let visible = core.layerIsVisible(layerID)
        let locked = core.layerIsLocked(layerID)
        HStack(spacing: 6) {
            Button {
                perform(visible ? "Hide Layer" : "Show Layer") {
                    core.setLayerVisible(layerID, visible: !visible)
                }
            } label: {
                Image(systemName: visible ? "eye.fill" : "eye.slash")
                    .foregroundStyle(.secondary)
            }
            .buttonStyle(.plain)
            Button {
                perform(locked ? "Unlock Layer" : "Lock Layer") {
                    core.setLayerLocked(layerID, locked: !locked)
                }
            } label: {
                Image(systemName: locked ? "lock.fill" : "lock.open")
                    .foregroundStyle(.secondary)
            }
            .buttonStyle(.plain)
            Text(core.layerName(layerID))
                .lineLimit(1)
            Spacer()
        }
        .font(.callout)
        .padding(.horizontal, 8)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(selected ? Color.accentColor.opacity(0.25) : Color.clear)
        .contentShape(Rectangle())
        .onTapGesture { editorState.selectedLayerID = layerID }
    }
}

// MARK: - Track row

private struct TrackRow: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    let property: TimelineProperty
    let duration: Int64
    let perform: (String, () -> Void) -> Void
    let registerEdit: (String) -> Void

    var body: some View {
        let path = property.rawValue
        let keyframes = core.keyframes(entityID: layerID, path: path)
        ZStack {
            Color.clear
            ForEach(keyframes) { keyframe in
                KeyframeDiamond(keyframe: keyframe, duration: duration) { from, to in
                    core.moveKeyframe(entityID: layerID, path: path, from: from, to: to)
                } onMoveEnded: {
                    core.endDrag()
                    registerEdit("Move Keyframe")
                } onDelete: {
                    perform("Delete Keyframe") {
                        core.removeKeyframe(entityID: layerID, path: path, frame: keyframe.frame)
                    }
                } onSetEasing: { easing in
                    perform("Set Easing") {
                        core.setEasing(entityID: layerID, path: path,
                                       frame: keyframe.frame, easing: easing)
                    }
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

// MARK: - Ruler canvas

private struct RulerCanvas: View {
    let duration: Int64
    let frameRate: Double

    var body: some View {
        Canvas { context, _ in
            let total = Int(duration)
            let second = max(Int(frameRate.rounded()), 1)
            let step = max(second / 2, 1)
            for frame in stride(from: 0, through: total, by: step) {
                let x = CGFloat(frame) * pixelsPerFrame
                let isSecond = frame % second == 0
                var tick = Path()
                tick.move(to: CGPoint(x: x, y: isSecond ? 8 : 14))
                tick.addLine(to: CGPoint(x: x, y: 22))
                context.stroke(tick, with: .color(.secondary), lineWidth: 1)
                if isSecond {
                    context.draw(Text("\(frame / second)s").font(.system(size: 9)),
                                 at: CGPoint(x: x + 10, y: 8), anchor: .leading)
                }
            }
        }
    }
}

// MARK: - Keyframe diamond

/// Draggable keyframe diamond. The drag's translation is measured from the
/// frame captured at drag start so live moves don't drift.
private struct KeyframeDiamond: View {
    let keyframe: KeyframeInfo
    let duration: Int64
    let onMove: (Int64, Int64) -> Void
    let onMoveEnded: () -> Void
    let onDelete: () -> Void
    let onSetEasing: (EasingInfo) -> Void

    @State private var dragStartFrame: Int64?

    var body: some View {
        Image(systemName: "diamond.fill")
            .font(.system(size: 11))
            .foregroundStyle(.yellow)
            .position(x: CGFloat(keyframe.frame) * pixelsPerFrame, y: rowHeight / 2)
            .gesture(
                DragGesture()
                    .onChanged { value in
                        if dragStartFrame == nil {
                            dragStartFrame = keyframe.frame
                        }
                        guard let start = dragStartFrame else { return }
                        let target = Int64(
                            (CGFloat(start) + value.translation.width / pixelsPerFrame).rounded()
                        )
                        let clamped = min(max(target, 0), duration)
                        if clamped != keyframe.frame {
                            onMove(keyframe.frame, clamped)
                        }
                    }
                    .onEnded { _ in
                        dragStartFrame = nil
                        onMoveEnded()
                    }
            )
            .contextMenu {
                Button("Delete Keyframe", role: .destructive, action: onDelete)
                Divider()
                Button("Linear") { onSetEasing(.linear) }
                Button("Ease In") { onSetEasing(.easeIn) }
                Button("Ease Out") { onSetEasing(.easeOut) }
                Button("Hold") { onSetEasing(.hold) }
            }
    }
}

// MARK: - Controls

private struct TimelineControls: View {
    @Bindable var editorState: EditorState
    let duration: Int64

    var body: some View {
        HStack(spacing: 12) {
            Button {
                editorState.isPlaying.toggle()
            } label: {
                Image(systemName: editorState.isPlaying ? "pause.fill" : "play.fill")
            }
            Text("\(editorState.playheadFrame) / \(duration)")
                .monospacedDigit()
                .font(.callout)
            Picker("Property", selection: $editorState.timelineProperty) {
                ForEach(TimelineProperty.allCases, id: \.self) { property in
                    Text(property.label).tag(property)
                }
            }
            .pickerStyle(.segmented)
            .frame(maxWidth: 320)
            Spacer()
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 4)
    }
}
