//
//  TimelineContentView.swift
//  MotionStudioApp
//
//  Timeline layout, split divider, scrolling, zoom, and playhead interaction.
//

import SwiftUI

struct TimelineContentView: View {
    @Environment(EditorState.self) private var editorState

    let duration: Int64
    let frameRate: Double
    let rows: [TimelineRow]
    let perform: (String, () -> Void) -> Void
    let registerEdit: (String) -> Void
    let clearSelection: () -> Void

    @State private var layerColumnWidth: CGFloat = minLayerColumnWidth
    @State private var isSplitDividerHovering = false
    @State private var isPlayheadHovering = false
    @State private var isTimeRangeDragging = false
    @State private var lastResolvedPointsPerFrame: CGFloat = pixelsPerFrame
    @State private var lastTimelineMagnification: CGFloat = 1
    @GestureState private var splitDragStartWidth: CGFloat?
    @GestureState private var timelineDragStartX: CGFloat?

    var body: some View {
        GeometryReader { proxy in
            let trackViewportWidth = max(1, proxy.size.width - layerColumnWidth - splitDividerWidth - trackLeadingInset)
            let pointsPerFrame = resolvedPointsPerFrame(duration: duration,
                                                        availableWidth: trackViewportWidth)
            let trackWidth = max(CGFloat(duration) * pointsPerFrame, trackViewportWidth)
            let maxScrollX = max(0, trackWidth - trackViewportWidth)
            let scrollX = clampedTimelineScrollX(maxScrollX: maxScrollX)
            let visiblePlayheadX = visibleTimelineX(for: editorState.playheadFrame,
                                                    pointsPerFrame: pointsPerFrame,
                                                    scrollX: scrollX)
            let contentPlayheadX = trackLeadingInset + visiblePlayheadX
            let contentViewportWidth = trackLeadingInset + trackViewportWidth

            VStack(alignment: .leading, spacing: 0) {
                TimelineControls(editorState: editorState, duration: duration)
                Divider()
                timelineWorkArea(trackViewportWidth: trackViewportWidth,
                                 pointsPerFrame: pointsPerFrame,
                                 trackWidth: trackWidth,
                                 scrollX: scrollX,
                                 visiblePlayheadX: visiblePlayheadX,
                                 contentPlayheadX: contentPlayheadX,
                                 contentViewportWidth: contentViewportWidth)
            }
            .frame(width: proxy.size.width, height: proxy.size.height, alignment: .topLeading)
            .onAppear {
                lastResolvedPointsPerFrame = pointsPerFrame
                clampTimelineScrollX(trackWidth: trackWidth, viewportWidth: trackViewportWidth)
            }
            .onChange(of: editorState.timelinePointsPerFrame) {
                preservePlayheadDuringZoom(from: lastResolvedPointsPerFrame,
                                           to: pointsPerFrame,
                                           trackWidth: trackWidth,
                                           viewportWidth: trackViewportWidth)
                lastResolvedPointsPerFrame = pointsPerFrame
            }
            .onChange(of: trackViewportWidth) {
                clampTimelineScrollX(trackWidth: trackWidth, viewportWidth: trackViewportWidth)
                lastResolvedPointsPerFrame = pointsPerFrame
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(Color.primary.opacity(0.04))
    }

    private func timelineWorkArea(trackViewportWidth: CGFloat, pointsPerFrame: CGFloat,
                                  trackWidth: CGFloat, scrollX: CGFloat,
                                  visiblePlayheadX: CGFloat, contentPlayheadX: CGFloat,
                                  contentViewportWidth: CGFloat) -> some View
    {
        ZStack(alignment: .topLeading) {
            VStack(spacing: 0) {
                TimelineHeaderStrip(layerColumnWidth: layerColumnWidth,
                                    splitDivider: splitDividerHitArea(height: rulerHeight),
                                    ruler: rulerStrip(pointsPerFrame: pointsPerFrame,
                                                      scrollX: scrollX,
                                                      visiblePlayheadX: visiblePlayheadX,
                                                      contentPlayheadX: contentPlayheadX,
                                                      contentViewportWidth: contentViewportWidth,
                                                      trackViewportWidth: trackViewportWidth,
                                                      trackWidth: trackWidth))
                Divider()
                TimelineBodyStrip(layerColumnWidth: layerColumnWidth,
                                  splitDivider: splitDividerHitArea(),
                                  graph: graphStrip(pointsPerFrame: pointsPerFrame,
                                                    scrollX: scrollX,
                                                    visiblePlayheadX: visiblePlayheadX,
                                                    contentPlayheadX: contentPlayheadX,
                                                    contentViewportWidth: contentViewportWidth,
                                                    trackViewportWidth: trackViewportWidth,
                                                    trackWidth: trackWidth),
                                  rows: rows,
                                  perform: perform,
                                  clearSelection: clearSelection)
            }
            GeometryReader { proxy in
                HorizontalSplitDivider(width: splitDividerWidth,
                                       isActive: isSplitDividerActive)
                    .frame(width: splitDividerWidth, height: proxy.size.height)
                    .offset(x: layerColumnWidth)
                    .allowsHitTesting(false)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .clipped()
    }

    private func rulerStrip(pointsPerFrame: CGFloat, scrollX: CGFloat,
                            visiblePlayheadX: CGFloat, contentPlayheadX: CGFloat,
                            contentViewportWidth: CGFloat, trackViewportWidth: CGFloat,
                            trackWidth: CGFloat) -> some View
    {
        ZStack(alignment: .topLeading) {
            RulerCanvas(duration: duration,
                        frameRate: frameRate,
                        pointsPerFrame: pointsPerFrame,
                        scrollX: scrollX,
                        contentInset: trackLeadingInset)
                .frame(width: contentViewportWidth, height: rulerHeight)
            Color.clear
                .contentShape(Rectangle())
                .frame(width: contentViewportWidth, height: rulerHeight)
                .gesture(scrubGesture(duration: duration,
                                      pointsPerFrame: pointsPerFrame,
                                      scrollX: scrollX))
                .simultaneousGesture(timelineMagnifyGesture(duration: duration,
                                                            pointsPerFrame: pointsPerFrame,
                                                            viewportWidth: trackViewportWidth,
                                                            contentWidth: contentViewportWidth))
            if isPlayheadVisible(visiblePlayheadX, viewportWidth: trackViewportWidth) {
                PlayheadLine(x: contentPlayheadX,
                             isHovering: isPlayheadHovering)
                    .allowsHitTesting(false)
            }
        }
        .frame(width: contentViewportWidth, height: rulerHeight)
        .overlay {
            timelinePointerInput(duration: duration,
                                 pointsPerFrame: pointsPerFrame,
                                 trackWidth: trackWidth,
                                 viewportWidth: trackViewportWidth,
                                 visiblePlayheadX: contentPlayheadX)
        }
        .clipped()
    }

    private func graphStrip(pointsPerFrame: CGFloat, scrollX: CGFloat,
                            visiblePlayheadX: CGFloat, contentPlayheadX: CGFloat,
                            contentViewportWidth: CGFloat, trackViewportWidth: CGFloat,
                            trackWidth: CGFloat) -> some View
    {
        ZStack(alignment: .topLeading) {
            Color.clear
                .contentShape(Rectangle())
                .onTapGesture(perform: clearSelection)
                .gesture(timelinePanGesture(trackWidth: trackWidth,
                                            viewportWidth: trackViewportWidth,
                                            playheadX: contentPlayheadX))
            timelineRows(pointsPerFrame: pointsPerFrame,
                         scrollX: scrollX,
                         contentViewportWidth: contentViewportWidth)
            playheadOverlay(visiblePlayheadX: visiblePlayheadX,
                            contentPlayheadX: contentPlayheadX,
                            pointsPerFrame: pointsPerFrame,
                            scrollX: scrollX,
                            trackViewportWidth: trackViewportWidth)
        }
        .frame(width: contentViewportWidth)
        .frame(maxHeight: .infinity, alignment: .top)
        .coordinateSpace(name: "timelineViewport")
        .overlay {
            timelinePointerInput(duration: duration,
                                 pointsPerFrame: pointsPerFrame,
                                 trackWidth: trackWidth,
                                 viewportWidth: trackViewportWidth,
                                 visiblePlayheadX: contentPlayheadX)
        }
        .simultaneousGesture(timelinePanGesture(trackWidth: trackWidth,
                                                viewportWidth: trackViewportWidth,
                                                playheadX: contentPlayheadX))
        .simultaneousGesture(timelineMagnifyGesture(duration: duration,
                                                    pointsPerFrame: pointsPerFrame,
                                                    viewportWidth: trackViewportWidth,
                                                    contentWidth: contentViewportWidth))
        .clipped()
    }

    private func timelineRows(pointsPerFrame: CGFloat, scrollX: CGFloat,
                              contentViewportWidth: CGFloat) -> some View
    {
        ZStack(alignment: .topLeading) {
            VStack(spacing: 0) {
                ForEach(rows) { row in
                    TrackRow(row: row,
                             duration: duration,
                             pointsPerFrame: pointsPerFrame,
                             scrollX: scrollX,
                             isTimeRangeDragging: $isTimeRangeDragging,
                             perform: perform,
                             registerEdit: registerEdit)
                        .frame(height: row.height)
                }
            }
        }
        .frame(width: contentViewportWidth)
        .frame(maxHeight: .infinity, alignment: .top)
    }

    private func playheadOverlay(visiblePlayheadX: CGFloat, contentPlayheadX: CGFloat,
                                 pointsPerFrame: CGFloat, scrollX: CGFloat,
                                 trackViewportWidth: CGFloat) -> some View
    {
        Group {
            if isPlayheadVisible(visiblePlayheadX, viewportWidth: trackViewportWidth) {
                PlayheadLine(x: contentPlayheadX,
                             isHovering: isPlayheadHovering,
                             showsMarker: false)
                    .allowsHitTesting(false)
                Rectangle()
                    .fill(Color.clear)
                    .contentShape(Rectangle())
                    .frame(width: 20)
                    .frame(maxHeight: .infinity)
                    .offset(x: contentPlayheadX - 10)
                    .onHover { isPlayheadHovering = $0 }
                    .gesture(playheadDrag(duration: duration,
                                          pointsPerFrame: pointsPerFrame,
                                          scrollX: scrollX))
            }
        }
    }

    private func timelinePointerInput(duration: Int64, pointsPerFrame: CGFloat,
                                      trackWidth: CGFloat, viewportWidth: CGFloat,
                                      visiblePlayheadX: CGFloat) -> some View
    {
        TimelinePointerInputView(editorState: editorState,
                                 isPlayheadHovering: $isPlayheadHovering,
                                 duration: duration,
                                 pointsPerFrame: pointsPerFrame,
                                 trackWidth: trackWidth,
                                 viewportWidth: viewportWidth,
                                 visiblePlayheadX: visiblePlayheadX,
                                 contentInset: trackLeadingInset)
    }

    private func scrubGesture(duration: Int64, pointsPerFrame: CGFloat,
                              scrollX: CGFloat) -> some Gesture
    {
        DragGesture(minimumDistance: 0)
            .onChanged { value in
                editorState.playheadFrame = timelineFrame(at: value.location.x,
                                                          pointsPerFrame: pointsPerFrame,
                                                          scrollX: scrollX,
                                                          duration: duration)
            }
    }

    private func playheadDrag(duration: Int64, pointsPerFrame: CGFloat,
                              scrollX: CGFloat) -> some Gesture
    {
        DragGesture(minimumDistance: 0, coordinateSpace: .named("timelineViewport"))
            .onChanged { value in
                editorState.playheadFrame = timelineFrame(at: value.location.x,
                                                          pointsPerFrame: pointsPerFrame,
                                                          scrollX: scrollX,
                                                          duration: duration)
            }
    }

    private func visibleTimelineX(for frame: Int64, pointsPerFrame: CGFloat,
                                  scrollX: CGFloat) -> CGFloat
    {
        timelineX(for: frame, pointsPerFrame: pointsPerFrame) - scrollX
    }

    private func timelineFrame(at visibleX: CGFloat, pointsPerFrame: CGFloat,
                               scrollX: CGFloat, duration: Int64) -> Int64
    {
        let frame = Int64(((visibleX - trackLeadingInset + scrollX) / pointsPerFrame).rounded())
        return min(max(frame, 0), duration)
    }

    private func isPlayheadVisible(_ visiblePlayheadX: CGFloat, viewportWidth: CGFloat) -> Bool {
        visiblePlayheadX >= 0 && visiblePlayheadX <= viewportWidth
    }

    private func resolvedPointsPerFrame(duration: Int64, availableWidth: CGFloat) -> CGFloat {
        let totalFrames = CGFloat(max(duration, 1))
        let fitPointsPerFrame = max(minTimelinePointsPerFrame, availableWidth / totalFrames)
        return min(max(CGFloat(editorState.timelinePointsPerFrame), fitPointsPerFrame), maxTimelinePointsPerFrame)
    }

    private func preservePlayheadDuringZoom(from oldPointsPerFrame: CGFloat,
                                            to newPointsPerFrame: CGFloat,
                                            trackWidth: CGFloat,
                                            viewportWidth: CGFloat)
    {
        guard oldPointsPerFrame > 0 else {
            clampTimelineScrollX(trackWidth: trackWidth, viewportWidth: viewportWidth)
            return
        }
        let oldVisibleX = visibleTimelineX(for: editorState.playheadFrame,
                                           pointsPerFrame: oldPointsPerFrame,
                                           scrollX: CGFloat(editorState.timelineScrollX))
        guard isPlayheadVisible(oldVisibleX, viewportWidth: viewportWidth) else {
            clampTimelineScrollX(trackWidth: trackWidth, viewportWidth: viewportWidth)
            return
        }
        let nextScrollX = timelineX(for: editorState.playheadFrame,
                                    pointsPerFrame: newPointsPerFrame) - oldVisibleX
        editorState.timelineScrollX = Double(clampedTimelineScrollX(nextScrollX,
                                                                    trackWidth: trackWidth,
                                                                    viewportWidth: viewportWidth))
    }

    private func timelinePanGesture(trackWidth: CGFloat, viewportWidth: CGFloat,
                                    playheadX: CGFloat? = nil) -> some Gesture
    {
        DragGesture(minimumDistance: 6)
            .updating($timelineDragStartX) { value, state, _ in
                guard !isTimeRangeDragging else { return }
                if state == nil {
                    state = isPlayheadDragStart(value.startLocation.x, playheadX: playheadX)
                        ? .nan
                        : CGFloat(editorState.timelineScrollX)
                }
            }
            .onChanged { value in
                guard !isTimeRangeDragging else { return }
                guard let start = timelineDragStartX, !start.isNaN else { return }
                let next = start - value.translation.width
                editorState.timelineScrollX = Double(clampedTimelineScrollX(next,
                                                                            trackWidth: trackWidth,
                                                                            viewportWidth: viewportWidth))
            }
    }

    private func isPlayheadDragStart(_ x: CGFloat, playheadX: CGFloat?) -> Bool {
        guard let playheadX else { return false }
        return abs(x - playheadX) <= 10
    }

    private func timelineMagnifyGesture(duration: Int64, pointsPerFrame: CGFloat,
                                        viewportWidth: CGFloat, contentWidth: CGFloat) -> some Gesture
    {
        MagnifyGesture(minimumScaleDelta: 0)
            .onChanged { value in
                let delta = value.magnification / lastTimelineMagnification
                lastTimelineMagnification = value.magnification
                let anchorX = value.startAnchor.x * contentWidth
                applyTimelineZoom(delta: delta,
                                  anchorX: anchorX,
                                  duration: duration,
                                  pointsPerFrame: pointsPerFrame,
                                  viewportWidth: viewportWidth)
            }
            .onEnded { _ in
                lastTimelineMagnification = 1
            }
    }

    private func applyTimelineZoom(delta: CGFloat, anchorX: CGFloat, duration: Int64,
                                   pointsPerFrame: CGFloat, viewportWidth: CGFloat)
    {
        guard delta.isFinite, delta > 0, pointsPerFrame > 0 else { return }
        let nextPointsPerFrame = min(max(pointsPerFrame * delta, minTimelinePointsPerFrame),
                                     maxTimelinePointsPerFrame)
        let anchorTimelineX = min(max(anchorX - trackLeadingInset, 0), viewportWidth)
        let frameUnderAnchor = (CGFloat(editorState.timelineScrollX) + anchorTimelineX) / pointsPerFrame
        let nextTrackWidth = max(CGFloat(duration) * nextPointsPerFrame, viewportWidth)
        let nextScrollX = frameUnderAnchor * nextPointsPerFrame - anchorTimelineX
        editorState.timelinePointsPerFrame = Double(nextPointsPerFrame)
        editorState.timelineScrollX = Double(clampedTimelineScrollX(nextScrollX,
                                                                    trackWidth: nextTrackWidth,
                                                                    viewportWidth: viewportWidth))
    }

    private func clampTimelineScrollX(trackWidth: CGFloat, viewportWidth: CGFloat) {
        let clamped = clampedTimelineScrollX(CGFloat(editorState.timelineScrollX),
                                             trackWidth: trackWidth,
                                             viewportWidth: viewportWidth)
        if CGFloat(editorState.timelineScrollX) != clamped {
            editorState.timelineScrollX = Double(clamped)
        }
    }

    private func clampedTimelineScrollX(maxScrollX: CGFloat) -> CGFloat {
        min(max(CGFloat(editorState.timelineScrollX), 0), maxScrollX)
    }

    private func clampedTimelineScrollX(_ value: CGFloat, trackWidth: CGFloat,
                                        viewportWidth: CGFloat) -> CGFloat
    {
        min(max(value, 0), max(0, trackWidth - viewportWidth))
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

private struct TimelineHeaderStrip<Ruler: View, SplitDivider: View>: View {
    let layerColumnWidth: CGFloat
    let splitDivider: SplitDivider
    let ruler: Ruler

    var body: some View {
        HStack(alignment: .top, spacing: 0) {
            LayerColumnHeader()
                .frame(width: layerColumnWidth, height: rulerHeight)
            splitDivider
            ruler
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .clipped()
    }
}

private struct TimelineBodyStrip<Graph: View, SplitDivider: View>: View {
    let layerColumnWidth: CGFloat
    let splitDivider: SplitDivider
    let graph: Graph
    let rows: [TimelineRow]
    let perform: (String, () -> Void) -> Void
    let clearSelection: () -> Void

    var body: some View {
        GeometryReader { scrollProxy in
            ScrollView(.vertical) {
                HStack(alignment: .top, spacing: 0) {
                    LayerColumn(rows: rows,
                                perform: perform,
                                clearSelection: clearSelection)
                        .frame(width: layerColumnWidth)
                        .frame(maxHeight: .infinity, alignment: .top)
                    splitDivider
                    graph
                }
                .frame(maxWidth: .infinity, minHeight: scrollProxy.size.height,
                       alignment: .topLeading)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }
}
