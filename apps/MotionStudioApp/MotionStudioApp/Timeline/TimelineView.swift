//
//  TimelineView.swift
//  MotionStudioApp
//
//  After-Effects/Figma-Motion-style timeline: a horizontally-frozen left layer
//  tree (layer rows plus an indented sub-row per animated property) and a right
//  time graph with a clip bar per layer and a keyframe lane per animated
//  property. The body scrolls vertically when rows overflow (layer tree and
//  graph stay in lockstep via a shared flattened row model); the ruler stays
//  pinned above. Horizontal pan/zoom is intentionally deferred, so the left
//  tree is frozen on both axes.
//

import SwiftUI

private let pixelsPerFrame: CGFloat = 6
private let layerRowHeight: CGFloat = 30
private let propertyRowHeight: CGFloat = 24
private let rulerHeight: CGFloat = 24
private let splitDividerWidth: CGFloat = 5
private let minLayerColumnWidth: CGFloat = 120
private let maxLayerColumnWidth: CGFloat = 480
private let layerActionIconSize: CGFloat = 16
private let layerActionButtonSize: CGFloat = 22

/// Animated properties offered as timeline sub-rows, in display order.
private struct PropertySpec {
    let path: String
    let label: String
}

private let transformSpecs: [PropertySpec] = [
    .init(path: "transform.position", label: "Position"),
    .init(path: "transform.scale", label: "Scale"),
    .init(path: "transform.rotation", label: "Rotation"),
    .init(path: "transform.opacity", label: "Opacity"),
]
private let shapeSizeSpec = PropertySpec(path: "elements[0].size", label: "Size")

/// One row of the flattened timeline model. Both the left tree and the right
/// graph iterate the same array so layer rows and property sub-rows line up
/// vertically for lockstep scrolling.
private struct TimelineRow: Identifiable {
    enum RowID: Hashable {
        case layer(UInt64)
        case property(UInt64, String)
    }

    let id: RowID
    let layerID: UInt64
    let isLayer: Bool
    let path: String?
    let label: String?

    var height: CGFloat {
        isLayer ? layerRowHeight : propertyRowHeight
    }
}

struct TimelineView: View {
    let document: MotionDocument
    let editorState: EditorState
    let perform: (String, () -> Void) -> Void
    let registerEdit: (String) -> Void
    let clearSelection: () -> Void

    @State private var layerColumnWidth: CGFloat = 200

    var body: some View {
        let core = document.core
        // Drives re-evaluation after every model mutation.
        let _ = core.revision
        let compositionID = core.firstCompositionID
        let duration = core.duration(compositionID: compositionID)
        let frameRate = core.frameRate(compositionID: compositionID)
        let layerIDs = Array(core.layerIDs(compositionID: compositionID).reversed())
        let rows = buildRows(core: core, layerIDs: layerIDs)
        let trackWidth = max(CGFloat(duration) * pixelsPerFrame, 1)
        let playheadX = CGFloat(editorState.playheadFrame) * pixelsPerFrame

        VStack(alignment: .leading, spacing: 0) {
            TimelineControls(editorState: editorState, duration: duration)
            Divider()
            // Header + body share one overlay playhead so the vertical line
            // stays continuous across the header/body divider instead of
            // appearing split into two segments.
            ZStack(alignment: .topLeading) {
                VStack(spacing: 0) {
                    // Pinned header: layer-column header + ruler (scrubbable).
                    HStack(alignment: .top, spacing: 0) {
                        LayerColumnHeader()
                            .frame(width: layerColumnWidth, height: rulerHeight)
                        HorizontalSplitDivider(width: splitDividerWidth,
                                               columnWidth: $layerColumnWidth)
                            .frame(height: rulerHeight)
                        ZStack(alignment: .topLeading) {
                            RulerCanvas(duration: duration, frameRate: frameRate)
                        }
                        .frame(width: trackWidth, height: rulerHeight)
                        .contentShape(Rectangle())
                        .gesture(scrubGesture(duration: duration))
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                    Divider()
                    // Vertically scrolling body: layer tree + graph in lockstep.
                    ScrollView(.vertical) {
                        HStack(alignment: .top, spacing: 0) {
                            LayerColumn(core: core, rows: rows, editorState: editorState,
                                        perform: perform, clearSelection: clearSelection)
                                .frame(width: layerColumnWidth)
                                .frame(maxHeight: .infinity, alignment: .top)
                            HorizontalSplitDivider(width: splitDividerWidth,
                                                   columnWidth: $layerColumnWidth)
                                .frame(maxHeight: .infinity)
                            ZStack(alignment: .topLeading) {
                                Color.clear
                                    .contentShape(Rectangle())
                                    .onTapGesture(perform: clearSelection)
                                VStack(spacing: 0) {
                                    ForEach(rows) { row in
                                        TrackRow(core: core,
                                                 row: row,
                                                 duration: duration,
                                                 perform: perform,
                                                 registerEdit: registerEdit)
                                            .frame(height: row.height)
                                    }
                                }
                                // Narrow grab strip so the playhead itself is
                                // draggable without stealing vertical scroll.
                                Rectangle()
                                    .fill(Color.clear)
                                    .contentShape(Rectangle())
                                    .frame(width: 12)
                                    .frame(maxHeight: .infinity)
                                    .offset(x: playheadX - 6)
                                    .gesture(playheadDrag(duration: duration))
                            }
                            .frame(width: trackWidth)
                            .frame(maxHeight: .infinity, alignment: .top)
                            .coordinateSpace(name: "tracks")
                        }
                        .frame(maxWidth: .infinity, maxHeight: .infinity,
                               alignment: .topLeading)
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
                PlayheadLine(x: layerColumnWidth + splitDividerWidth + playheadX)
                    .allowsHitTesting(false)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        // Faint panel tint so empty lane areas read as a filled panel flush to
        // the window edges rather than as gaps.
        .background(Color.primary.opacity(0.04))
    }

    /// Flattens the layer stack into a lockstep row sequence: each layer emits a
    /// layer row followed by one sub-row per currently-animated property.
    private func buildRows(core: MotionDocumentCore, layerIDs: [UInt64]) -> [TimelineRow] {
        var rows: [TimelineRow] = []
        for layerID in layerIDs {
            rows.append(TimelineRow(id: .layer(layerID), layerID: layerID,
                                    isLayer: true, path: nil, label: nil))
            for spec in transformSpecs where core.isAnimated(entityID: layerID, path: spec.path) {
                rows.append(TimelineRow(id: .property(layerID, spec.path), layerID: layerID,
                                        isLayer: false, path: spec.path, label: spec.label))
            }
            if core.isAnimated(entityID: layerID, path: shapeSizeSpec.path) {
                rows.append(TimelineRow(id: .property(layerID, shapeSizeSpec.path),
                                        layerID: layerID, isLayer: false,
                                        path: shapeSizeSpec.path, label: shapeSizeSpec.label))
            }
        }
        return rows
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

// MARK: - Layer column (tree)

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
    let rows: [TimelineRow]
    let editorState: EditorState
    let perform: (String, () -> Void) -> Void
    let clearSelection: () -> Void

    var body: some View {
        ZStack(alignment: .topLeading) {
            Color.clear
                .contentShape(Rectangle())
                .onTapGesture(perform: clearSelection)
            VStack(spacing: 0) {
                ForEach(rows) { row in
                    if row.isLayer {
                        LayerRow(core: core, layerID: row.layerID,
                                 editorState: editorState, perform: perform)
                            .frame(height: row.height)
                    } else {
                        PropertySubRow(core: core, layerID: row.layerID,
                                       label: row.label ?? "", path: row.path ?? "",
                                       editorState: editorState)
                            .frame(height: row.height)
                    }
                }
            }
        }
        .frame(maxHeight: .infinity, alignment: .topLeading)
    }
}

private struct LayerRow: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    let editorState: EditorState
    let perform: (String, () -> Void) -> Void

    var body: some View {
        // Re-render on any document mutation; bridge reads don't trigger observation.
        let _ = core.revision
        let selected = editorState.selectedLayerID == layerID
        let visible = core.layerIsVisible(layerID)
        let locked = core.layerIsLocked(layerID)
        HStack(spacing: 6) {
            Image(systemName: layerSymbol(core.layerType(layerID)))
                .foregroundStyle(.secondary)
            Text(core.layerName(layerID))
                .lineLimit(1)
            Spacer()
            Button {
                perform(visible ? "Hide Layer" : "Show Layer") {
                    core.setLayerVisible(layerID, visible: !visible)
                }
            } label: {
                Image(systemName: visible ? "eye.fill" : "eye.slash")
                    .foregroundStyle(.secondary)
                    .font(.system(size: layerActionIconSize))
                    .frame(width: layerActionIconSize, height: layerActionIconSize)
            }
            .frame(width: layerActionButtonSize, height: layerActionButtonSize)
            .contentShape(Rectangle())
            .buttonStyle(.plain)
            Button {
                perform(locked ? "Unlock Layer" : "Lock Layer") {
                    core.setLayerLocked(layerID, locked: !locked)
                }
            } label: {
                Image(systemName: locked ? "lock.fill" : "lock.open")
                    .foregroundStyle(.secondary)
                    .font(.system(size: layerActionIconSize))
                    .frame(width: layerActionIconSize, height: layerActionIconSize)
            }
            .frame(width: layerActionButtonSize, height: layerActionButtonSize)
            .contentShape(Rectangle())
            .buttonStyle(.plain)
        }
        .font(.callout)
        .padding(.horizontal, 8)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(selected ? Color.accentColor.opacity(0.25) : Color.clear)
        .contentShape(Rectangle())
        .onTapGesture { editorState.selectedLayerID = layerID }
    }
}

/// Indented sub-row naming one animated property. The trailing diamond mirrors
/// the inspector: filled when a keyframe sits on the playhead. Tapping selects
/// the owning layer so the inspector follows.
private struct PropertySubRow: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    let label: String
    let path: String
    let editorState: EditorState

    var body: some View {
        let hasKeyframe = core.keyframes(entityID: layerID, path: path)
            .contains { $0.frame == editorState.playheadFrame }
        HStack(spacing: 4) {
            Text(label)
                .foregroundStyle(.secondary)
            Spacer()
            if hasKeyframe {
                Image(systemName: "diamond.fill")
                    .font(.system(size: 8))
                    .foregroundStyle(.yellow)
            }
        }
        .font(.caption)
        .padding(.leading, 28)
        .padding(.trailing, 8)
        .frame(maxWidth: .infinity, alignment: .leading)
        .contentShape(Rectangle())
        .onTapGesture { editorState.selectedLayerID = layerID }
    }
}

/// Maps the bridge MS_LAYER_* type tag to an SF Symbol. Shape layers (the only
/// kind creatable today) fall through to the default square glyph.
private func layerSymbol(_ type: Int32) -> String {
    switch type {
    case 1:
        return "photo"
    case 2:
        return "textformat"
    case 3:
        return "circle.dashed"
    case 4:
        return "film"
    default:
        return "square"
    }
}

// MARK: - Track row (graph)

private struct TrackRow: View {
    let core: MotionDocumentCore
    let row: TimelineRow
    let duration: Int64
    let perform: (String, () -> Void) -> Void
    let registerEdit: (String) -> Void

    var body: some View {
        if row.isLayer {
            ClipBarView(core: core, layerID: row.layerID)
        } else {
            PropertyLaneView(core: core,
                             layerID: row.layerID,
                             path: row.path ?? "",
                             label: row.label ?? "",
                             duration: duration,
                             perform: perform,
                             registerEdit: registerEdit)
        }
    }
}

/// Static clip bar spanning the layer's in/out points: a hatched rounded strip
/// with end handles. Drawn in one Canvas so the hatching clips to the corners.
private struct ClipBarView: View {
    let core: MotionDocumentCore
    let layerID: UInt64

    var body: some View {
        let inPoint = CGFloat(core.layerInPoint(layerID))
        let outPoint = CGFloat(core.layerOutPoint(layerID))
        let originX = inPoint * pixelsPerFrame
        let barWidth = max((outPoint - inPoint) * pixelsPerFrame, 2)

        Canvas { context, size in
            let rect = CGRect(x: originX, y: 4, width: barWidth, height: size.height - 8)
            let outline = Path(roundedRect: rect, cornerRadius: 4)
            context.fill(outline, with: .color(Color.secondary.opacity(0.12)))

            context.clip(to: outline)
            var stripes = Path()
            let step: CGFloat = 6
            var stripeX = rect.minX - rect.height
            while stripeX < rect.maxX {
                stripes.move(to: CGPoint(x: stripeX, y: rect.maxY))
                stripes.addLine(to: CGPoint(x: stripeX + rect.height, y: rect.minY))
                stripeX += step
            }
            context.stroke(stripes, with: .color(Color.secondary.opacity(0.18)), lineWidth: 1)

            context.stroke(outline, with: .color(Color.secondary.opacity(0.35)), lineWidth: 1)
            for handleX in [rect.minX, rect.maxX] {
                let handle = Path(roundedRect: CGRect(x: handleX - 1.5, y: rect.minY + 2,
                                                      width: 3, height: rect.height - 4),
                                  cornerRadius: 1.5)
                context.fill(handle, with: .color(Color.secondary.opacity(0.5)))
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

/// One animated property's lane: a span bar over the first/last keyframe with
/// the property name centered in it, plus draggable keyframe diamonds.
private struct PropertyLaneView: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    let path: String
    let label: String
    let duration: Int64
    let perform: (String, () -> Void) -> Void
    let registerEdit: (String) -> Void

    var body: some View {
        let keyframes = core.keyframes(entityID: layerID, path: path)
        let frames = keyframes.map(\.frame)
        let firstFrame = frames.min()
        let lastFrame = frames.max()

        ZStack(alignment: .topLeading) {
            Color.clear
            if let firstFrame, let lastFrame {
                let barX = CGFloat(firstFrame) * pixelsPerFrame
                let barWidth = max(CGFloat(lastFrame - firstFrame) * pixelsPerFrame, 2)
                let barHeight = propertyRowHeight - 8
                RoundedRectangle(cornerRadius: 4)
                    .fill(Color.secondary.opacity(0.06))
                    .overlay(RoundedRectangle(cornerRadius: 4)
                        .stroke(Color.secondary.opacity(0.4), lineWidth: 1))
                    .overlay(spanLabel(barWidth: barWidth))
                    .frame(width: barWidth, height: barHeight)
                    .offset(x: barX, y: (propertyRowHeight - barHeight) / 2)
            }
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

    @ViewBuilder
    private func spanLabel(barWidth: CGFloat) -> some View {
        if barWidth > 40 {
            Text(label)
                .font(.system(size: 9))
                .foregroundStyle(.secondary)
                .lineLimit(1)
                .padding(.horizontal, 4)
        }
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

// MARK: - Playhead

/// Continuous playhead drawn as a single overlay above the header/body divider:
/// a blue triangle capping the ruler plus one blue line that runs unbroken from
/// the ruler top down to the bottom of the visible track area. `x` is the
/// playhead's horizontal center in the overlay's coordinate space.
private struct PlayheadLine: View {
    let x: CGFloat

    var body: some View {
        // The triangle is a hand-drawn shape (not an SF Symbol) so its top edge
        // sits exactly at y=0, flush with the line top: a symbol glyph carries
        // built-in top padding that made the line poke above the triangle. The
        // triangle is drawn after the line so it covers the line's top, keeping
        // the join seamless while their tops align.
        ZStack(alignment: .top) {
            Rectangle()
                .fill(.blue)
                .frame(width: 1.5)
                .frame(maxHeight: .infinity, alignment: .top)
            PlayheadTriangle()
                .fill(.blue)
                .frame(width: 9, height: 7)
        }
        .frame(maxHeight: .infinity, alignment: .top)
        .fixedSize(horizontal: true, vertical: false)
        .offset(x: x - 4.5)
    }
}

/// Downward triangle whose top edge spans the full frame width at y=0 and whose
/// apex is centered at the bottom, so it can cap the playhead line with its top
/// exactly flush to the line top.
private struct PlayheadTriangle: Shape {
    func path(in rect: CGRect) -> Path {
        var path = Path()
        path.move(to: CGPoint(x: rect.minX, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.maxX, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.midX, y: rect.maxY))
        path.closeSubpath()
        return path
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
            .position(x: CGFloat(keyframe.frame) * pixelsPerFrame, y: propertyRowHeight / 2)
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
            // Global space-to-play/pause; a window-level key equivalent so it
            // fires regardless of which panel holds focus (the canvas
            // included). A text field being edited consumes the space first.
            .keyboardShortcut(.space, modifiers: [])

            Text("\(editorState.playheadFrame) / \(duration)")
                .monospacedDigit()
                .font(.callout)
            Spacer()
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 4)
    }
}
