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
#if os(macOS)
    import AppKit
#endif

private let pixelsPerFrame: CGFloat = 6
private let rowHeight: CGFloat = 28
private let rulerHeight: CGFloat = 24
private let splitDividerWidth: CGFloat = 5
private let minLayerColumnWidth: CGFloat = 120
private let maxLayerColumnWidth: CGFloat = 480

struct TimelineView: View {
    let document: MotionDocument
    let editorState: EditorState
    let perform: (String, () -> Void) -> Void
    let registerEdit: (String) -> Void

    @State private var layerColumnWidth: CGFloat = 200

    var body: some View {
        let core = document.core
        // Drives re-evaluation after every model mutation.
        let _ = core.revision
        let compositionID = core.firstCompositionID
        let duration = core.duration(compositionID: compositionID)
        let frameRate = core.frameRate(compositionID: compositionID)
        let layerIDs = Array(core.layerIDs(compositionID: compositionID).reversed())
        let trackWidth = max(CGFloat(duration) * pixelsPerFrame, 1)

        VStack(alignment: .leading, spacing: 0) {
            TimelineControls(editorState: editorState, duration: duration)
            Divider()
            // Pinned header: layer-column header + ruler (scrubbable).
            HStack(alignment: .top, spacing: 0) {
                LayerColumnHeader()
                    .frame(width: layerColumnWidth, height: rulerHeight)
                HorizontalSplitDivider(width: splitDividerWidth,
                                       columnWidth: $layerColumnWidth)
                    .frame(height: rulerHeight)
                ZStack(alignment: .topLeading) {
                    RulerCanvas(duration: duration, frameRate: frameRate)
                    Rectangle()
                        .fill(.red)
                        .frame(width: 1.5, height: rulerHeight)
                        .offset(x: CGFloat(editorState.playheadFrame) * pixelsPerFrame)
                        .allowsHitTesting(false)
                }
                .frame(width: trackWidth, height: rulerHeight)
                .contentShape(Rectangle())
                .gesture(scrubGesture(duration: duration))
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            Divider()
            // Vertically scrolling body: layer stack + graph in lockstep.
            ScrollView(.vertical) {
                HStack(alignment: .top, spacing: 0) {
                    LayerColumn(core: core, layerIDs: layerIDs, editorState: editorState,
                                perform: perform)
                        .frame(width: layerColumnWidth)
                        .frame(maxHeight: .infinity, alignment: .top)
                    HorizontalSplitDivider(width: splitDividerWidth,
                                           columnWidth: $layerColumnWidth)
                        .frame(maxHeight: .infinity)
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
                            .frame(width: 1.5)
                            .frame(maxHeight: .infinity)
                            .offset(x: CGFloat(editorState.playheadFrame) * pixelsPerFrame)
                            .allowsHitTesting(false)
                        // Narrow grab strip so the playhead itself is draggable
                        // without stealing vertical scroll from the rest of the lane.
                        Rectangle()
                            .fill(Color.clear)
                            .contentShape(Rectangle())
                            .frame(width: 12)
                            .frame(maxHeight: .infinity)
                            .offset(x: CGFloat(editorState.playheadFrame) * pixelsPerFrame - 6)
                            .gesture(playheadDrag(duration: duration))
                    }
                    .frame(width: trackWidth)
                    .frame(maxHeight: .infinity, alignment: .top)
                    .coordinateSpace(name: "tracks")
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            }
            .frame(maxWidth: .infinity)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        // Faint panel tint so empty lane areas read as a filled panel flush to
        // the window edges rather than as gaps.
        .background(Color.primary.opacity(0.04))
    }

    private func scrubGesture(duration: Int64) -> some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { value in
                let frame = Int64((value.location.x / pixelsPerFrame).rounded())
                editorState.playheadFrame = min(max(frame, 0), duration)
            }
    }

    /// Drags the playhead by reading the touch position in the (horizontally
    /// stable) tracks coordinate space, so vertical scrolling doesn't drift it.
    private func playheadDrag(duration: Int64) -> some Gesture {
        DragGesture(minimumDistance: 0, coordinateSpace: .named("tracks"))
            .onChanged { value in
                let frame = Int64((value.location.x / pixelsPerFrame).rounded())
                editorState.playheadFrame = min(max(frame, 0), duration)
            }
    }
}

// MARK: - Horizontal split divider

/// Draggable vertical divider that resizes the layer column. Placed in both the
/// pinned header and the scrolling body so it reads as one continuous handle. A
/// literal HSplitView can't be used here: it would force the body into two
/// scroll views and break the vertical lockstep between layer rows and tracks.
private struct HorizontalSplitDivider: View {
    let width: CGFloat
    @Binding var columnWidth: CGFloat
    @GestureState private var startWidth: CGFloat?

    var body: some View {
        Rectangle()
            .fill(Color.secondary.opacity(0.2))
            .overlay(Rectangle().fill(.separator).frame(width: 1))
            .frame(width: width)
            .contentShape(Rectangle())
        #if os(macOS)
            .onHover { hovering in
                if hovering {
                    NSCursor.resizeLeftRight.push()
                } else {
                    NSCursor.pop()
                }
            }
        #endif
            .gesture(
                DragGesture(minimumDistance: 0)
                    .updating($startWidth) { _, state, _ in
                        if state == nil {
                            state = columnWidth
                        }
                    }
                    .onChanged { value in
                        guard let start = startWidth else { return }
                        let next = start + value.translation.width
                        columnWidth = min(max(next, minLayerColumnWidth), maxLayerColumnWidth)
                    }
            )
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
            Spacer()
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 4)
    }
}
