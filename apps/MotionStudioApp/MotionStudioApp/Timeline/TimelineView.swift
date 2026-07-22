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

struct TimelineView: View {
    let document: MotionDocument
    let editorState: EditorState
    let perform: (String, () -> Void) -> Void
    let registerEdit: (String) -> Void
    let clearSelection: () -> Void

    @State private var layerColumnWidth: CGFloat = minLayerColumnWidth
    @State private var isSplitDividerHovering = false
    @State private var isPlayheadHovering = false
    @GestureState private var splitDragStartWidth: CGFloat?

    var body: some View {
        let core = document.core
        // Drives re-evaluation after every model mutation.
        let _ = core.revision
        let compositionID = core.firstCompositionID
        let duration = core.duration(compositionID: compositionID)
        let frameRate = core.frameRate(compositionID: compositionID)
        let layerIDs = Array(core.layerIDs(compositionID: compositionID).reversed())
        let rows = buildRows(core: core, layerIDs: layerIDs, editorState: editorState)
        let trackWidth = max(CGFloat(duration) * pixelsPerFrame, 1)
        let playheadX = timelineX(for: editorState.playheadFrame)

        VStack(alignment: .leading, spacing: 0) {
            TimelineControls(editorState: editorState, duration: duration)
            Divider()
            // Header + body share overlay handles so vertical markers stay
            // continuous instead of appearing split across the pinned header and
            // scrolling body.
            ZStack(alignment: .topLeading) {
                VStack(spacing: 0) {
                    HStack(alignment: .top, spacing: 0) {
                        LayerColumnHeader()
                            .frame(width: layerColumnWidth, height: rulerHeight)
                        splitDividerHitArea(height: rulerHeight)
                        Color.clear
                            .frame(width: trackLeadingInset, height: rulerHeight)
                        RulerCanvas(duration: duration, frameRate: frameRate)
                            .frame(width: trackWidth, height: rulerHeight)
                            .contentShape(Rectangle())
                            .gesture(scrubGesture(duration: duration))
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                    Divider()
                    ScrollView(.vertical) {
                        HStack(alignment: .top, spacing: 0) {
                            LayerColumn(core: core, rows: rows, editorState: editorState,
                                        perform: perform, clearSelection: clearSelection)
                                .frame(width: layerColumnWidth)
                                .frame(maxHeight: .infinity, alignment: .top)
                            splitDividerHitArea()
                            Color.clear
                                .frame(width: trackLeadingInset)
                            ZStack(alignment: .topLeading) {
                                Color.clear
                                    .contentShape(Rectangle())
                                    .onTapGesture(perform: clearSelection)
                                VStack(spacing: 0) {
                                    ForEach(rows) { row in
                                        TrackRow(core: core,
                                                 row: row,
                                                 duration: duration,
                                                 editorState: editorState,
                                                 perform: perform,
                                                 registerEdit: registerEdit)
                                            .frame(height: row.height)
                                    }
                                }
                                Rectangle()
                                    .fill(Color.clear)
                                    .contentShape(Rectangle())
                                    .frame(width: 12)
                                    .frame(maxHeight: .infinity)
                                    .offset(x: playheadX - 6)
                                    .onHover { isPlayheadHovering = $0 }
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
                GeometryReader { proxy in
                    HorizontalSplitDivider(width: splitDividerWidth,
                                           isActive: isSplitDividerActive)
                        .frame(width: splitDividerWidth, height: proxy.size.height)
                        .offset(x: layerColumnWidth)
                        .allowsHitTesting(false)
                }
                PlayheadLine(x: layerColumnWidth + splitDividerWidth + trackLeadingInset + playheadX,
                             isHovering: isPlayheadHovering)
                    .allowsHitTesting(false)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(Color.primary.opacity(0.04))
    }

    private func buildRows(core: MotionDocumentCore, layerIDs: [UInt64],
                           editorState: EditorState) -> [TimelineRow] {
        var rows: [TimelineRow] = []
        for layerID in layerIDs {
            rows.append(TimelineRow(id: .layer(layerID), layerID: layerID, kind: .layer))
            let animatedPaths = Set(timelineAnimatedPropertyPaths(core: core, layerID: layerID))
            for property in TransformProperty.allCases where animatedPaths.contains(property.path) {
                rows.append(propertyRow(core: core,
                                        layerID: layerID,
                                        path: property.path,
                                        label: property.actionLabel))
            }
            if animatedPaths.contains(timelineShapeSizePath) {
                rows.append(propertyRow(core: core,
                                        layerID: layerID,
                                        path: timelineShapeSizePath,
                                        label: "Size"))
            }
        }
        return rows
    }

    private func propertyRow(core: MotionDocumentCore, layerID: UInt64, path: String,
                             label: String) -> TimelineRow {
        if timelineUsesManualKeyframeTrack(core: core, layerID: layerID, path: path) {
            return TimelineRow(id: .keyframeTrack(layerID, path), layerID: layerID,
                               kind: .keyframeTrack(path: path, label: label))
        }
        return TimelineRow(id: .propertySpan(layerID, path), layerID: layerID,
                           kind: .propertySpan(path: path, label: label))
    }

    private func scrubGesture(duration: Int64) -> some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { value in
                let frame = Int64((value.location.x / pixelsPerFrame).rounded())
                editorState.playheadFrame = min(max(frame, 0), duration)
            }
    }

    private func playheadDrag(duration: Int64) -> some Gesture {
        DragGesture(minimumDistance: 0, coordinateSpace: .named("tracks"))
            .onChanged { value in
                let frame = Int64((value.location.x / pixelsPerFrame).rounded())
                editorState.playheadFrame = min(max(frame, 0), duration)
            }
    }

    private func splitDividerDrag() -> some Gesture {
        DragGesture(minimumDistance: 0)
            .updating($splitDragStartWidth) { _, state, _ in
                if state == nil {
                    state = layerColumnWidth
                }
            }
            .onChanged { value in
                guard let start = splitDragStartWidth else { return }
                let next = start + value.translation.width
                layerColumnWidth = min(max(next, minLayerColumnWidth), maxLayerColumnWidth)
            }
    }

    private func splitDividerHitArea(height: CGFloat? = nil) -> some View {
        Color.clear
            .frame(width: splitDividerWidth)
            .frame(height: height)
            .contentShape(Rectangle())
            .onHover { isSplitDividerHovering = $0 }
            .gesture(splitDividerDrag())
    }

    private var isSplitDividerActive: Bool {
        isSplitDividerHovering || splitDragStartWidth != nil
    }
}
