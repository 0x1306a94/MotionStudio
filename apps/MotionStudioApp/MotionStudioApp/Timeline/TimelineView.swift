//
//  TimelineView.swift
//  MotionStudioApp
//
//  After-Effects/Figma-Motion-style timeline: a horizontally-frozen left layer
//  tree (layer rows plus an indented sub-row per animated property) and a right
//  time graph with a clip bar per layer and a keyframe lane per animated
//  property. The body scrolls vertically when rows overflow (layer tree and
//  graph stay in lockstep via a shared flattened row model); the ruler stays
//  pinned above while the time graph supports horizontal pan and scale.
//

import SwiftUI
import UIKit

struct TimelineView: View {
    let document: MotionProjectState
    let editorState: EditorState
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
        let core = document.core
        // Drives re-evaluation after every model mutation.
        let _ = core.revision
        let compositionID = core.firstCompositionID
        let duration = core.duration(compositionID: compositionID)
        let frameRate = core.frameRate(compositionID: compositionID)
        let layerIDs = Array(core.layerIDs(compositionID: compositionID).reversed())
        let rows = buildRows(core: core, layerIDs: layerIDs, editorState: editorState)

        GeometryReader { proxy in
            let trackViewportWidth = max(1, proxy.size.width - layerColumnWidth - splitDividerWidth - trackLeadingInset)
            let pointsPerFrame = pointsPerFrame(duration: duration, availableWidth: trackViewportWidth)
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
                // Header + body share overlay handles so vertical markers stay
                // continuous instead of appearing split across the pinned header and
                // scrolling body.
                ZStack(alignment: .topLeading) {
                    VStack(spacing: 0) {
                        HStack(alignment: .top, spacing: 0) {
                            LayerColumnHeader()
                                .frame(width: layerColumnWidth, height: rulerHeight)
                            splitDividerHitArea(height: rulerHeight)
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
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .clipped()
                        Divider()
                        GeometryReader { scrollProxy in
                            ScrollView(.vertical) {
                                HStack(alignment: .top, spacing: 0) {
                                    LayerColumn(core: core, rows: rows, editorState: editorState,
                                                perform: perform, clearSelection: clearSelection)
                                        .frame(width: layerColumnWidth)
                                        .frame(maxHeight: .infinity, alignment: .top)
                                    splitDividerHitArea()
                                    ZStack(alignment: .topLeading) {
                                        Color.clear
                                            .contentShape(Rectangle())
                                            .onTapGesture(perform: clearSelection)
                                            .gesture(timelinePanGesture(trackWidth: trackWidth,
                                                                        viewportWidth: trackViewportWidth,
                                                                        playheadX: contentPlayheadX))
                                        ZStack(alignment: .topLeading) {
                                            VStack(spacing: 0) {
                                                ForEach(rows) { row in
                                                    TrackRow(core: core,
                                                             row: row,
                                                             duration: duration,
                                                             pointsPerFrame: pointsPerFrame,
                                                             scrollX: scrollX,
                                                             editorState: editorState,
                                                             isTimeRangeDragging: $isTimeRangeDragging,
                                                             perform: perform,
                                                             registerEdit: registerEdit)
                                                        .frame(height: row.height)
                                                }
                                            }
                                        }
                                        .frame(width: contentViewportWidth)
                                        .frame(maxHeight: .infinity, alignment: .top)
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
                                .frame(maxWidth: .infinity, minHeight: scrollProxy.size.height,
                                       alignment: .topLeading)
                            }
                            .frame(maxWidth: .infinity, maxHeight: .infinity)
                        }
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

    private func buildRows(core: MotionDocumentCore, layerIDs: [UInt64],
                           editorState _: EditorState) -> [TimelineRow]
    {
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
                             label: String) -> TimelineRow
    {
        if timelineUsesManualKeyframeTrack(core: core, layerID: layerID, path: path) {
            return TimelineRow(id: .keyframeTrack(layerID, path), layerID: layerID,
                               kind: .keyframeTrack(path: path, label: label))
        }
        return TimelineRow(id: .propertySpan(layerID, path), layerID: layerID,
                           kind: .propertySpan(path: path, label: label))
    }

    private func pointsPerFrame(duration: Int64, availableWidth: CGFloat) -> CGFloat {
        let totalFrames = CGFloat(max(duration, 1))
        let fitPointsPerFrame = max(minTimelinePointsPerFrame, availableWidth / totalFrames)
        return min(max(CGFloat(editorState.timelinePointsPerFrame), fitPointsPerFrame), maxTimelinePointsPerFrame)
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

private struct TimelinePointerInputView: UIViewRepresentable {
    let editorState: EditorState
    @Binding var isPlayheadHovering: Bool
    let duration: Int64
    let pointsPerFrame: CGFloat
    let trackWidth: CGFloat
    let viewportWidth: CGFloat
    let visiblePlayheadX: CGFloat
    let contentInset: CGFloat

    func makeUIView(context: Context) -> UIView {
        let view = TimelinePointerPassthroughView(frame: .zero)
        view.backgroundColor = .clear

        let hoverGesture = UIHoverGestureRecognizer(target: context.coordinator,
                                                    action: #selector(Coordinator.handleHover(_:)))
        hoverGesture.cancelsTouchesInView = false
        view.addGestureRecognizer(hoverGesture)

        let pinchGesture = UIPinchGestureRecognizer(target: context.coordinator,
                                                    action: #selector(Coordinator.handlePinch(_:)))
        pinchGesture.cancelsTouchesInView = false
        pinchGesture.delegate = context.coordinator
        context.coordinator.pinchGesture = pinchGesture
        view.addGestureRecognizer(pinchGesture)

        let scrollGesture = UIPanGestureRecognizer(target: context.coordinator,
                                                   action: #selector(Coordinator.handleScroll(_:)))
        scrollGesture.maximumNumberOfTouches = 0
        scrollGesture.allowedScrollTypesMask = [.continuous, .discrete]
        scrollGesture.cancelsTouchesInView = false
        scrollGesture.delegate = context.coordinator
        view.addGestureRecognizer(scrollGesture)

        return view
    }

    func updateUIView(_: UIView, context: Context) {
        context.coordinator.editorState = editorState
        context.coordinator.isPlayheadHovering = $isPlayheadHovering
        context.coordinator.duration = duration
        context.coordinator.pointsPerFrame = pointsPerFrame
        context.coordinator.trackWidth = trackWidth
        context.coordinator.viewportWidth = viewportWidth
        context.coordinator.visiblePlayheadX = visiblePlayheadX
        context.coordinator.contentInset = contentInset
    }

    func makeCoordinator() -> Coordinator {
        Coordinator(editorState: editorState,
                    isPlayheadHovering: $isPlayheadHovering,
                    duration: duration,
                    pointsPerFrame: pointsPerFrame,
                    trackWidth: trackWidth,
                    viewportWidth: viewportWidth,
                    visiblePlayheadX: visiblePlayheadX,
                    contentInset: contentInset)
    }

    @MainActor
    final class Coordinator: NSObject, UIGestureRecognizerDelegate {
        var editorState: EditorState
        var isPlayheadHovering: Binding<Bool>
        var duration: Int64
        var pointsPerFrame: CGFloat
        var trackWidth: CGFloat
        var viewportWidth: CGFloat
        var visiblePlayheadX: CGFloat
        var contentInset: CGFloat
        weak var pinchGesture: UIPinchGestureRecognizer?
        private var lastPinchScale: CGFloat = 1
        private var lastScrollTranslation: CGPoint = .zero
        private var lastPointerX: CGFloat?

        init(editorState: EditorState,
             isPlayheadHovering: Binding<Bool>,
             duration: Int64,
             pointsPerFrame: CGFloat,
             trackWidth: CGFloat,
             viewportWidth: CGFloat,
             visiblePlayheadX: CGFloat,
             contentInset: CGFloat)
        {
            self.editorState = editorState
            self.isPlayheadHovering = isPlayheadHovering
            self.duration = duration
            self.pointsPerFrame = pointsPerFrame
            self.trackWidth = trackWidth
            self.viewportWidth = viewportWidth
            self.visiblePlayheadX = visiblePlayheadX
            self.contentInset = contentInset
        }

        @objc func handleHover(_ gesture: UIHoverGestureRecognizer) {
            switch gesture.state {
            case .began, .changed:
                let pointerX = gesture.location(in: gesture.view).x
                lastPointerX = pointerX
                isPlayheadHovering.wrappedValue = abs(pointerX - visiblePlayheadX) <= 10
            default:
                isPlayheadHovering.wrappedValue = false
            }
        }

        @objc func handlePinch(_ gesture: UIPinchGestureRecognizer) {
            switch gesture.state {
            case .began:
                lastPinchScale = gesture.scale
            case .changed:
                let delta = gesture.scale / lastPinchScale
                lastPinchScale = gesture.scale
                applyZoom(delta: delta, anchorX: gesture.location(in: gesture.view).x)
            default:
                lastPinchScale = 1
            }
        }

        @objc func handleScroll(_ gesture: UIPanGestureRecognizer) {
            if let pinchGesture, pinchGesture.state == .began || pinchGesture.state == .changed {
                lastScrollTranslation = gesture.translation(in: gesture.view)
                return
            }
            switch gesture.state {
            case .began:
                lastScrollTranslation = .zero
            case .changed:
                let translation = gesture.translation(in: gesture.view)
                let delta = CGPoint(x: translation.x - lastScrollTranslation.x,
                                    y: translation.y - lastScrollTranslation.y)
                lastScrollTranslation = translation
                if gesture.modifierFlags.contains(.command) {
                    let fallbackAnchor = (gesture.view?.bounds.width ?? viewportWidth) * 0.5
                    applyZoom(delta: exp(-delta.y * 0.01), anchorX: lastPointerX ?? fallbackAnchor)
                } else {
                    applyScroll(delta: timelineScrollDelta(delta: delta,
                                                           modifierFlags: gesture.modifierFlags))
                }
            default:
                lastScrollTranslation = .zero
            }
        }

        func gestureRecognizer(_: UIGestureRecognizer,
                               shouldRecognizeSimultaneouslyWith _: UIGestureRecognizer) -> Bool
        {
            true
        }

        private func timelineScrollDelta(delta: CGPoint,
                                         modifierFlags: UIKeyModifierFlags) -> CGFloat
        {
            if delta.x != 0 {
                return delta.x
            }
            if modifierFlags.contains(.shift) {
                return delta.y
            }
            return 0
        }

        private func applyScroll(delta: CGFloat) {
            guard delta != 0 else { return }
            let next = CGFloat(editorState.timelineScrollX) - delta
            editorState.timelineScrollX = Double(clampedScrollX(next, trackWidth: trackWidth))
        }

        private func applyZoom(delta: CGFloat, anchorX: CGFloat) {
            guard delta.isFinite, delta > 0, pointsPerFrame > 0 else { return }
            let nextPointsPerFrame = min(max(pointsPerFrame * delta, minTimelinePointsPerFrame),
                                         maxTimelinePointsPerFrame)
            let anchorTimelineX = min(max(anchorX - contentInset, 0), viewportWidth)
            let frameUnderAnchor = (CGFloat(editorState.timelineScrollX) + anchorTimelineX) / pointsPerFrame
            let nextTrackWidth = max(CGFloat(duration) * nextPointsPerFrame, viewportWidth)
            let nextScrollX = frameUnderAnchor * nextPointsPerFrame - anchorTimelineX
            editorState.timelinePointsPerFrame = Double(nextPointsPerFrame)
            editorState.timelineScrollX = Double(clampedScrollX(nextScrollX, trackWidth: nextTrackWidth))
        }

        private func clampedScrollX(_ value: CGFloat, trackWidth: CGFloat) -> CGFloat {
            min(max(value, 0), max(0, trackWidth - viewportWidth))
        }
    }
}

private final class TimelinePointerPassthroughView: UIView {
    override func point(inside point: CGPoint, with event: UIEvent?) -> Bool {
        guard let event else {
            return super.point(inside: point, with: event)
        }
        if event.type == .touches, (event.allTouches?.count ?? 1) <= 1 {
            return false
        }
        return super.point(inside: point, with: event)
    }
}
