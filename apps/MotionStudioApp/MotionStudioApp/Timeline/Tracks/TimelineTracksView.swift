//
//  TimelineTracksView.swift
//  MotionStudioApp
//
//  Layer time-range bars, property spans, and keyframe tracks.
//

import MotionStudioBridging
import UIKit

@MainActor
protocol TimelineTracksViewDelegate: AnyObject {
    func timelineTracksDidScroll(_ tracks: TimelineTracksView, offsetY: CGFloat)
    func timelineTracksTimeRangeDraggingChanged(_ tracks: TimelineTracksView, isDragging: Bool)
}

struct TimelineEasingPresentationRequest {
    let easing: EasingInfo
    let easingAffectsPlayback: Bool
    let sourceView: UIView
    let onSetEasing: (EasingInfo) -> Void
    let onDelete: (() -> Void)?
    let onCommit: () -> Void
    let onDragBegan: () -> Void
    let onDragEnded: () -> Void
}

@MainActor
final class TimelineTracksView: UIView {
    weak var delegate: TimelineTracksViewDelegate?
    var onPresentEasing: ((TimelineEasingPresentationRequest) -> Void)?

    private let document: MotionProjectState
    private let editorState: EditorState
    private let performEdit: (String, () -> Void) -> Void
    private let registerEdit: (String) -> Void

    private let scrollView = UIScrollView()
    private let contentView = UIView()
    private var contentHeightConstraint: NSLayoutConstraint?
    private var rowViews: [TimelineTrackRowView] = []
    private var rows: [TimelineRow] = []
    private var duration: Int64 = 0
    private var pointsPerFrame: CGFloat = pixelsPerFrame
    private var scrollX: CGFloat = 0
    private var isSyncingOffset = false
    /// moveKeyframe bumps revision → reload; rebuild would destroy the active pan recognizer.
    private var suspendsReload = false
    private var needsReloadAfterInteraction = false

    init(document: MotionProjectState,
         editorState: EditorState,
         perform: @escaping (String, () -> Void) -> Void,
         registerEdit: @escaping (String) -> Void)
    {
        self.document = document
        self.editorState = editorState
        performEdit = perform
        self.registerEdit = registerEdit
        super.init(frame: .zero)
        translatesAutoresizingMaskIntoConstraints = false
        configureScroll()
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    var contentOffsetY: CGFloat {
        get { scrollView.contentOffset.y }
        set {
            guard abs(scrollView.contentOffset.y - newValue) > 0.5 else {
                return
            }
            isSyncingOffset = true
            scrollView.contentOffset = CGPoint(x: 0, y: newValue)
            isSyncingOffset = false
        }
    }

    /// Applies a vertical wheel/trackpad delta (positive = content moves down).
    /// Allows rubber-banding past edges (UITableView-like); call `endVerticalRubberBand()` on gesture end.
    func scrollVertically(by deltaY: CGFloat) {
        guard deltaY != 0 else {
            return
        }
        let limits = verticalScrollLimits()
        let proposed = scrollView.contentOffset.y - deltaY
        let next: CGFloat = if proposed < limits.min {
            limits.min - rubberBand(overscroll: limits.min - proposed, dimension: scrollView.bounds.height)
        } else if proposed > limits.max {
            limits.max + rubberBand(overscroll: proposed - limits.max, dimension: scrollView.bounds.height)
        } else {
            proposed
        }
        guard abs(next - scrollView.contentOffset.y) > 0.05 else {
            return
        }
        contentOffsetY = next
        delegate?.timelineTracksDidScroll(self, offsetY: next)
    }

    /// Springs content offset back into valid range after trackpad/wheel rubber-band.
    func endVerticalRubberBand() {
        let limits = verticalScrollLimits()
        let current = scrollView.contentOffset.y
        let clamped = min(max(current, limits.min), limits.max)
        guard abs(clamped - current) > 0.5 else {
            return
        }
        isSyncingOffset = true
        UIView.animate(withDuration: 0.35,
                       delay: 0,
                       usingSpringWithDamping: 1,
                       initialSpringVelocity: 0,
                       options: [.allowUserInteraction, .beginFromCurrentState, .curveEaseOut])
        {
            self.scrollView.contentOffset = CGPoint(x: 0, y: clamped)
        } completion: { _ in
            self.isSyncingOffset = false
            self.delegate?.timelineTracksDidScroll(self, offsetY: self.scrollView.contentOffset.y)
        }
        // Keep sidebar locked to the settled edge immediately (same final offset).
        delegate?.timelineTracksDidScroll(self, offsetY: clamped)
    }

    private func verticalScrollLimits() -> (min: CGFloat, max: CGFloat) {
        let minOffset = -scrollView.adjustedContentInset.top
        let maxOffset = max(minOffset,
                            scrollView.contentSize.height - scrollView.bounds.height
                                + scrollView.adjustedContentInset.bottom)
        return (minOffset, maxOffset)
    }

    /// UIScrollView-style rubber band: maps overscroll distance into a diminishing offset.
    private func rubberBand(overscroll: CGFloat, dimension: CGFloat) -> CGFloat {
        let limit = max(dimension, 1)
        let constant: CGFloat = 0.55
        return (1 - (1 / ((overscroll * constant / limit) + 1))) * limit
    }

    func reload(rows: [TimelineRow], duration: Int64, pointsPerFrame: CGFloat, scrollX: CGFloat) {
        self.rows = rows
        self.duration = duration
        self.pointsPerFrame = pointsPerFrame
        self.scrollX = scrollX
        if suspendsReload {
            // Do not relayout here — keyframe diamonds / property handles recreate subviews
            // and would cancel the active pan (same failure mode as early timeRangeBar).
            // Live visuals come from onDragMoved callbacks.
            needsReloadAfterInteraction = true
            return
        }
        rebuildRows()
    }

    func updateHorizontalMetrics(pointsPerFrame: CGFloat, scrollX: CGFloat) {
        self.pointsPerFrame = pointsPerFrame
        self.scrollX = scrollX
        guard !suspendsReload else {
            return
        }
        for rowView in rowViews {
            rowView.updateMetrics(pointsPerFrame: pointsPerFrame, scrollX: scrollX, duration: duration)
        }
    }

    func refreshSelectionAppearance() {
        guard !suspendsReload else {
            return
        }
        for rowView in rowViews {
            rowView.refreshSelection()
        }
    }

    fileprivate func beginInteractiveEdit() {
        suspendsReload = true
    }

    fileprivate func endInteractiveEdit() {
        suspendsReload = false
        if needsReloadAfterInteraction {
            needsReloadAfterInteraction = false
            rebuildRows()
        }
    }

    private func configureScroll() {
        scrollView.translatesAutoresizingMaskIntoConstraints = false
        scrollView.delegate = self
        scrollView.alwaysBounceVertical = true
        scrollView.bounces = true
        scrollView.showsVerticalScrollIndicator = false
        scrollView.backgroundColor = .clear
        contentView.translatesAutoresizingMaskIntoConstraints = false
        addSubview(scrollView)
        scrollView.addSubview(contentView)
        NSLayoutConstraint.activate([
            scrollView.topAnchor.constraint(equalTo: topAnchor),
            scrollView.leadingAnchor.constraint(equalTo: leadingAnchor),
            scrollView.trailingAnchor.constraint(equalTo: trailingAnchor),
            scrollView.bottomAnchor.constraint(equalTo: bottomAnchor),
            contentView.topAnchor.constraint(equalTo: scrollView.contentLayoutGuide.topAnchor),
            contentView.leadingAnchor.constraint(equalTo: scrollView.contentLayoutGuide.leadingAnchor),
            contentView.trailingAnchor.constraint(equalTo: scrollView.contentLayoutGuide.trailingAnchor),
            contentView.bottomAnchor.constraint(equalTo: scrollView.contentLayoutGuide.bottomAnchor),
            contentView.widthAnchor.constraint(equalTo: scrollView.frameLayoutGuide.widthAnchor),
        ])
        let clearTap = UITapGestureRecognizer(target: self, action: #selector(handleBackgroundTap))
        clearTap.cancelsTouchesInView = false
        scrollView.addGestureRecognizer(clearTap)
    }

    @objc private func handleBackgroundTap(_ recognizer: UITapGestureRecognizer) {
        let point = recognizer.location(in: contentView)
        if rowViews.contains(where: { $0.frame.contains(point) && $0.hitTest(contentView.convert(point, to: $0), with: nil) != nil }) {
            return
        }
        editorState.clearLayerSelection()
    }

    private func rebuildRows() {
        rowViews.forEach { $0.removeFromSuperview() }
        rowViews.removeAll()
        var top: CGFloat = 0
        for row in rows {
            let rowView = TimelineTrackRowView(document: document,
                                               editorState: editorState,
                                               row: row,
                                               duration: duration,
                                               pointsPerFrame: pointsPerFrame,
                                               scrollX: scrollX,
                                               perform: performEdit,
                                               registerEdit: registerEdit)
            rowView.onTimeRangeDraggingChanged = { [weak self] isDragging in
                guard let self else {
                    return
                }
                if isDragging {
                    beginInteractiveEdit()
                } else {
                    endInteractiveEdit()
                }
                delegate?.timelineTracksTimeRangeDraggingChanged(self, isDragging: isDragging)
            }
            rowView.onDragMoved = { [weak self, weak rowView] scope in
                guard let self, let rowView else {
                    return
                }
                let layerID = rowView.layerID
                switch scope {
                case .entireLayer:
                    for view in rowViews where view.layerID == layerID {
                        if view === rowView, view.isLayerRow {
                            view.refreshTimeRangeOnly()
                        } else if view === rowView {
                            view.refreshContentPreservingGestures()
                        } else {
                            view.updateMetrics(pointsPerFrame: pointsPerFrame, scrollX: scrollX, duration: duration)
                        }
                    }
                case .rowAndLayerBar:
                    // Geometry only — full reloadContent risks cancelling property handle pan.
                    rowView.refreshPropertySpanOnly()
                    for view in rowViews where view.layerID == layerID && view.isLayerRow {
                        view.refreshTimeRangeOnly()
                    }
                case .layerBarOnly:
                    for view in rowViews where view.layerID == layerID && view.isLayerRow {
                        view.refreshTimeRangeOnly()
                    }
                }
            }
            rowView.onPresentEasing = { [weak self] request in
                self?.onPresentEasing?(request)
            }
            rowView.translatesAutoresizingMaskIntoConstraints = false
            contentView.addSubview(rowView)
            NSLayoutConstraint.activate([
                rowView.topAnchor.constraint(equalTo: contentView.topAnchor, constant: top),
                rowView.leadingAnchor.constraint(equalTo: contentView.leadingAnchor),
                rowView.trailingAnchor.constraint(equalTo: contentView.trailingAnchor),
                rowView.heightAnchor.constraint(equalToConstant: row.height),
            ])
            rowViews.append(rowView)
            top += row.height
        }
        if let contentHeightConstraint {
            contentHeightConstraint.constant = max(top, 1)
        } else {
            let constraint = contentView.heightAnchor.constraint(equalToConstant: max(top, 1))
            constraint.isActive = true
            contentHeightConstraint = constraint
        }
    }
}

extension TimelineTracksView: UIScrollViewDelegate {
    func scrollViewDidScroll(_ scrollView: UIScrollView) {
        guard !isSyncingOffset else {
            return
        }
        delegate?.timelineTracksDidScroll(self, offsetY: scrollView.contentOffset.y)
    }
}

// MARK: - Row

private enum TimelineDragRefreshScope {
    case entireLayer
    case rowAndLayerBar
    /// Keyframe diamond pan: update envelope only (rebuilding diamonds would cancel the gesture).
    case layerBarOnly
}

@MainActor
private final class TimelineTrackRowView: UIView {
    var onTimeRangeDraggingChanged: ((Bool) -> Void)?
    var onDragMoved: ((TimelineDragRefreshScope) -> Void)?
    var onPresentEasing: ((TimelineEasingPresentationRequest) -> Void)?

    var layerID: UInt64 {
        row.layerID
    }

    var isLayerRow: Bool {
        if case .layer = row.kind {
            return true
        }
        return false
    }

    private let document: MotionProjectState
    private let editorState: EditorState
    private let row: TimelineRow
    private var duration: Int64
    private var pointsPerFrame: CGFloat
    private var scrollX: CGFloat
    private let performEdit: (String, () -> Void) -> Void
    private let registerEdit: (String) -> Void

    private let timeRangeBar = TimelineTimeRangeBarView()
    private let propertyBar = TimelinePropertyBarView()
    private var segmentViews: [UIView] = []
    private var diamondViews: [TimelineKeyframeDiamondView] = []

    private var dragSession: TimelineDragSession?
    private var dragFrameOffset: Int64 = 0
    private var dragCurrentFrames: [String: Int64] = [:]
    private var didDrag = false
    private var dragEditName: String?

    init(document: MotionProjectState,
         editorState: EditorState,
         row: TimelineRow,
         duration: Int64,
         pointsPerFrame: CGFloat,
         scrollX: CGFloat,
         perform: @escaping (String, () -> Void) -> Void,
         registerEdit: @escaping (String) -> Void)
    {
        self.document = document
        self.editorState = editorState
        self.row = row
        self.duration = duration
        self.pointsPerFrame = pointsPerFrame
        self.scrollX = scrollX
        performEdit = perform
        self.registerEdit = registerEdit
        super.init(frame: .zero)
        clipsToBounds = true
        addSubview(timeRangeBar)
        addSubview(propertyBar)
        timeRangeBar.isHidden = true
        propertyBar.isHidden = true
        wireTimeRangeGestures()
        reloadContent()
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    func updateMetrics(pointsPerFrame: CGFloat, scrollX: CGFloat, duration: Int64) {
        self.pointsPerFrame = pointsPerFrame
        self.scrollX = scrollX
        self.duration = duration
        reloadContent()
    }

    func refreshSelection() {
        reloadContent()
    }

    func refreshTimeRangeOnly() {
        layoutTimeRange()
    }

    func refreshPropertySpanOnly() {
        guard case let .propertySpan(path, label) = row.kind else {
            return
        }
        layoutPropertySpan(path: path, label: label)
    }

    func refreshContentPreservingGestures() {
        reloadContent()
    }

    private func reloadContent() {
        segmentViews.forEach { $0.removeFromSuperview() }
        segmentViews.removeAll()
        diamondViews.forEach { $0.removeFromSuperview() }
        diamondViews.removeAll()
        // Do not hide timeRangeBar / propertyBar before relayout — toggling isHidden cancels an active pan.
        switch row.kind {
        case .layer:
            propertyBar.isHidden = true
            layoutTimeRange()
        case let .propertySpan(path, label):
            timeRangeBar.isHidden = true
            layoutPropertySpan(path: path, label: label)
        case let .keyframeTrack(path, _):
            timeRangeBar.isHidden = true
            propertyBar.isHidden = true
            layoutKeyframeTrack(path: path)
        }
    }

    private func layoutTimeRange() {
        let core = document.core
        let paths = timelineAnimatedPropertyPaths(core: core, layerID: row.layerID)
        guard let range = keyframeRange(paths: paths) else {
            timeRangeBar.isHidden = true
            return
        }
        let selected = editorState.isLayerSelected(row.layerID)
        let startX = contentX(for: range.startFrame)
        let endX = contentX(for: range.endFrame)
        let spanWidth = max(endX - startX, 2)
        let barHeight = layerRowHeight - 8
        timeRangeBar.isHidden = false
        timeRangeBar.configure(isSelected: selected)
        timeRangeBar.frame = CGRect(x: startX, y: (layerRowHeight - barHeight) / 2, width: spanWidth, height: barHeight)
    }

    private func layoutPropertySpan(path: String, label: String) {
        let frames = document.core.keyframes(entityID: row.layerID, path: path).map(\.frame)
        guard let firstFrame = frames.min(), let lastFrame = frames.max(), firstFrame < lastFrame else {
            propertyBar.isHidden = true
            return
        }
        let selected = editorState.isLayerSelected(row.layerID)
            || editorState.selectedTimelineProperty == TimelinePropertySelection(layerID: row.layerID, path: path)
        let startX = contentX(for: firstFrame)
        let endX = contentX(for: lastFrame)
        let spanWidth = max(endX - startX, 2)
        let barHeight = propertyRowHeight - 8
        propertyBar.isHidden = false
        propertyBar.configure(label: label, isSelected: selected)
        propertyBar.frame = CGRect(x: startX, y: (propertyRowHeight - barHeight) / 2, width: spanWidth, height: barHeight)
        propertyBar.onTap = { [weak self] in
            guard let self else {
                return
            }
            editorState.selectedLayerID = row.layerID
            editorState.selectedTimelineProperty = TimelinePropertySelection(layerID: row.layerID, path: path)
            editorState.selectedTimelineSegment = nil
        }
    }

    private func layoutKeyframeTrack(path: String) {
        let keyframes = document.core.keyframes(entityID: row.layerID, path: path).sorted { $0.frame < $1.frame }
        let segments = zip(keyframes, keyframes.dropFirst()).map(KeyframeSegment.init)
        let trackSelected = editorState.isLayerSelected(row.layerID)
        for segment in segments {
            let startX = contentX(for: segment.start.frame)
            let endX = contentX(for: segment.end.frame)
            let width = max(endX - startX, 2)
            let selected = trackSelected
                || editorState.selectedTimelineSegment == TimelineSegmentSelection(layerID: row.layerID,
                                                                                   path: path,
                                                                                   startFrame: segment.start.frame,
                                                                                   endFrame: segment.end.frame)
            let segmentView = TimelineKeyframeSegmentView(frame: CGRect(x: startX, y: 0, width: width, height: propertyRowHeight))
            segmentView.configure(easing: segment.start.easing, isSelected: selected)
            let tap = UITapGestureRecognizer(target: self, action: #selector(handleSegmentTap(_:)))
            segmentView.addGestureRecognizer(tap)
            segmentView.isUserInteractionEnabled = true
            segmentView.accessibilityIdentifier = "\(segment.start.frame):\(segment.end.frame):\(path)"
            addSubview(segmentView)
            segmentViews.append(segmentView)
        }
        for (index, keyframe) in keyframes.enumerated() {
            let frame = keyframe.frame
            let selected = trackSelected || isKeyframeSelected(frame, path: path)
            let diamond = TimelineKeyframeDiamondView()
            diamond.configure(selected: selected)
            diamond.center = CGPoint(x: contentX(for: frame), y: propertyRowHeight / 2)
            let prev = index > 0 ? keyframes[index - 1].frame : nil as Int64?
            let next = index + 1 < keyframes.count ? keyframes[index + 1].frame : nil as Int64?
            var dragOriginFrame: Int64?
            diamond.onMoved = { [weak self] translationWidth in
                guard let self else {
                    return
                }
                if dragOriginFrame == nil {
                    dragOriginFrame = frame
                    beginKeyframeDrag(path: path, frame: frame)
                }
                guard let origin = dragOriginFrame else {
                    return
                }
                let pointer = Int64((CGFloat(origin) + translationWidth / pointsPerFrame).rounded())
                updateKeyframeDrag(path: path, originFrame: origin, pointerFrame: pointer, prev: prev, next: next)
                syncKeyframeTrackGeometry(path: path)
            }
            diamond.onMoveEnded = { [weak self] in
                self?.endInteractiveDrag()
            }
            diamond.onDelete = { [weak self] in
                guard let self else {
                    return
                }
                let current = dragCurrentFrames[dragKey(path: path, origin: frame)] ?? frame
                performEdit("Delete Keyframe") {
                    self.document.core.removeKeyframe(entityID: self.row.layerID, path: path, frame: current)
                }
            }
            diamond.onSelect = { [weak self] in
                guard let self else {
                    return
                }
                let current = dragCurrentFrames[dragKey(path: path, origin: frame)] ?? frame
                editorState.selectedLayerID = row.layerID
                editorState.selectedTimelineProperty = TimelinePropertySelection(layerID: row.layerID, path: path)
                let hasOutgoing = index + 1 < keyframes.count
                if hasOutgoing {
                    let nextFrame = keyframes[index + 1].frame
                    editorState.selectedTimelineSegment = TimelineSegmentSelection(layerID: row.layerID,
                                                                                   path: path,
                                                                                   startFrame: current,
                                                                                   endFrame: nextFrame)
                } else {
                    editorState.selectedTimelineSegment = nil
                }
                onPresentEasing?(TimelineEasingPresentationRequest(
                    easing: keyframe.easing,
                    easingAffectsPlayback: hasOutgoing,
                    sourceView: diamond,
                    onSetEasing: { easing in
                        self.document.core.setEasing(entityID: self.row.layerID, path: path, frame: current, easing: easing)
                    },
                    onDelete: {
                        self.performEdit("Delete Keyframe") {
                            self.document.core.removeKeyframe(entityID: self.row.layerID, path: path, frame: current)
                        }
                    },
                    onCommit: { self.registerEdit("Set Easing") },
                    onDragBegan: { self.document.core.beginDrag() },
                    onDragEnded: {
                        self.document.core.endDrag()
                        self.registerEdit("Set Easing")
                    },
                ))
            }
            addSubview(diamond)
            diamondViews.append(diamond)
        }
        for diamond in diamondViews {
            bringSubviewToFront(diamond)
        }
    }

    private func isKeyframeSelected(_ frame: Int64, path: String) -> Bool {
        guard let selection = editorState.selectedTimelineSegment,
              selection.layerID == row.layerID,
              selection.path == path
        else {
            return false
        }
        return frame == selection.startFrame || frame == selection.endFrame
    }

    private func contentX(for frame: Int64) -> CGFloat {
        trackLeadingInset + timelineX(for: frame, pointsPerFrame: pointsPerFrame) - scrollX
    }

    private func keyframeRange(paths: [String]) -> TimeRangeDraft? {
        let frames = paths.flatMap { document.core.keyframes(entityID: row.layerID, path: $0).map(\.frame) }
        let uniqueFrames = Array(Set(frames)).sorted()
        guard let firstFrame = uniqueFrames.first,
              let lastFrame = uniqueFrames.last,
              firstFrame < lastFrame
        else {
            return nil
        }
        let leadingMaxFrame = uniqueFrames.dropFirst().first.map { $0 - 1 } ?? lastFrame - 1
        let trailingMinFrame = uniqueFrames.dropLast().last.map { $0 + 1 } ?? firstFrame + 1
        return TimeRangeDraft(startFrame: firstFrame,
                              endFrame: lastFrame,
                              leadingMaxFrame: leadingMaxFrame,
                              trailingMinFrame: trailingMinFrame)
    }

    private func wireTimeRangeGestures() {
        let bodyTap = UITapGestureRecognizer(target: self, action: #selector(selectLayerRow))
        timeRangeBar.addGestureRecognizer(bodyTap)
        let leading = UIPanGestureRecognizer(target: self, action: #selector(handleLayerLeadingDrag(_:)))
        let trailing = UIPanGestureRecognizer(target: self, action: #selector(handleLayerTrailingDrag(_:)))
        timeRangeBar.leadingHandle.addGestureRecognizer(leading)
        timeRangeBar.trailingHandle.addGestureRecognizer(trailing)

        let propertyLeading = UIPanGestureRecognizer(target: self, action: #selector(handlePropertyLeadingDrag(_:)))
        let propertyTrailing = UIPanGestureRecognizer(target: self, action: #selector(handlePropertyTrailingDrag(_:)))
        propertyBar.leadingHandle.addGestureRecognizer(propertyLeading)
        propertyBar.trailingHandle.addGestureRecognizer(propertyTrailing)
    }

    @objc private func selectLayerRow() {
        editorState.selectedLayerID = row.layerID
        editorState.selectedTimelineProperty = nil
        editorState.selectedTimelineSegment = nil
    }

    @objc private func handleLayerLeadingDrag(_ recognizer: UIPanGestureRecognizer) {
        handleLayerScaleDrag(edge: .leading, recognizer: recognizer)
    }

    @objc private func handleLayerTrailingDrag(_ recognizer: UIPanGestureRecognizer) {
        handleLayerScaleDrag(edge: .trailing, recognizer: recognizer)
    }

    @objc private func handlePropertyLeadingDrag(_ recognizer: UIPanGestureRecognizer) {
        handlePropertyEdgeDrag(edge: .leading, recognizer: recognizer)
    }

    @objc private func handlePropertyTrailingDrag(_ recognizer: UIPanGestureRecognizer) {
        handlePropertyEdgeDrag(edge: .trailing, recognizer: recognizer)
    }

    private func handleLayerScaleDrag(edge: TimeRangeDragEdge, recognizer: UIPanGestureRecognizer) {
        switch recognizer.state {
        case .began, .changed:
            onTimeRangeDraggingChanged?(true)
            let locationX = recognizer.location(in: self).x
            updateLayerScaleDrag(edge: edge, locationX: locationX)
        case .ended, .cancelled, .failed:
            endInteractiveDrag()
        default:
            break
        }
    }

    private func updateLayerScaleDrag(edge: TimeRangeDragEdge, locationX: CGFloat) {
        let pointerFrame = Int64(((locationX - trackLeadingInset + scrollX) / pointsPerFrame).rounded())
        if dragSession == nil {
            let paths = timelineAnimatedPropertyPaths(core: document.core, layerID: row.layerID)
            guard let range = keyframeRange(paths: paths) else {
                return
            }
            var originFrames: [(path: String, frame: Int64)] = []
            for path in paths {
                for keyframe in document.core.keyframes(entityID: row.layerID, path: path) {
                    originFrames.append((path, keyframe.frame))
                }
            }
            guard let session = TimelineDragEngine.makeLayerScaleSession(edge: edge,
                                                                         originStart: range.startFrame,
                                                                         originEnd: range.endFrame,
                                                                         originFrames: originFrames)
            else {
                return
            }
            beginSession(session, pointerFrame: pointerFrame, edgeFrame: range.frame(for: edge), editName: "Scale Time Range")
            selectLayerRow()
        }
        guard let session = dragSession else {
            return
        }
        let newEdge = pointerFrame - dragFrameOffset
        let moves = TimelineDragEngine.resolve(session: session,
                                               pointerFrame: newEdge,
                                               duration: duration,
                                               neighbors: nil)
        applyMoves(moves)
        if didDrag {
            onDragMoved?(.entireLayer)
        }
    }

    private func handlePropertyEdgeDrag(edge: TimeRangeDragEdge, recognizer: UIPanGestureRecognizer) {
        guard case let .propertySpan(path, _) = row.kind else {
            return
        }
        switch recognizer.state {
        case .began, .changed:
            onTimeRangeDraggingChanged?(true)
            let locationX = recognizer.location(in: self).x
            updatePropertyEdgeDrag(path: path, edge: edge, locationX: locationX)
        case .ended, .cancelled, .failed:
            endInteractiveDrag()
        default:
            break
        }
    }

    private func updatePropertyEdgeDrag(path: String, edge: TimeRangeDragEdge, locationX: CGFloat) {
        let pointerFrame = Int64(((locationX - trackLeadingInset + scrollX) / pointsPerFrame).rounded())
        if dragSession == nil {
            let frames = document.core.keyframes(entityID: row.layerID, path: path).map(\.frame)
            guard let start = frames.min(), let end = frames.max(), start < end,
                  let session = TimelineDragEngine.makePropertyEdgeSession(path: path,
                                                                           edge: edge,
                                                                           originStart: start,
                                                                           originEnd: end)
            else {
                return
            }
            let edgeFrame = edge == .leading ? start : end
            beginSession(session, pointerFrame: pointerFrame, edgeFrame: edgeFrame, editName: "Move Property Range")
            editorState.selectedLayerID = row.layerID
            editorState.selectedTimelineProperty = TimelinePropertySelection(layerID: row.layerID, path: path)
            editorState.selectedTimelineSegment = nil
        }
        guard let session = dragSession else {
            return
        }
        let newEdge = pointerFrame - dragFrameOffset
        let moves = TimelineDragEngine.resolve(session: session,
                                               pointerFrame: newEdge,
                                               duration: duration,
                                               neighbors: nil)
        applyMoves(moves)
        if didDrag {
            onDragMoved?(.rowAndLayerBar)
        }
    }

    private func beginKeyframeDrag(path: String, frame: Int64) {
        onTimeRangeDraggingChanged?(true)
        let session = TimelineDragEngine.makeKeyframeSession(path: path, frame: frame)
        dragSession = session
        dragFrameOffset = 0
        dragCurrentFrames = [dragKey(path: path, origin: frame): frame]
        didDrag = false
        dragEditName = "Move Keyframe"
        document.core.beginDrag()
    }

    private func updateKeyframeDrag(path _: String, originFrame _: Int64, pointerFrame: Int64,
                                    prev: Int64?, next: Int64?)
    {
        guard let session = dragSession else {
            return
        }
        let moves = TimelineDragEngine.resolve(session: session,
                                               pointerFrame: pointerFrame,
                                               duration: duration,
                                               neighbors: (prev: prev, next: next))
        applyMoves(moves)
        if didDrag {
            onDragMoved?(.layerBarOnly)
        }
    }

    private func beginSession(_ session: TimelineDragSession,
                              pointerFrame: Int64,
                              edgeFrame: Int64,
                              editName: String)
    {
        dragSession = session
        dragFrameOffset = pointerFrame - edgeFrame
        dragCurrentFrames = Dictionary(uniqueKeysWithValues: session.originFrames.map {
            (dragKey(path: $0.path, origin: $0.frame), $0.frame)
        })
        if case let .propertyEdge(path) = session.scope, let edge = session.edge {
            let origin = edge == .leading ? session.originStart : session.originEnd
            dragCurrentFrames[dragKey(path: path, origin: origin)] = origin
        }
        didDrag = false
        dragEditName = editName
        document.core.beginDrag()
    }

    private func applyMoves(_ moves: [KeyframeMove]) {
        let enriched = moves.compactMap { move -> (KeyframeMove, Int64)? in
            let key = dragKey(path: move.path, origin: move.from)
            let current = dragCurrentFrames[key] ?? move.from
            guard current != move.to else {
                return nil
            }
            return (move, current)
        }
        let ordered = enriched.sorted { lhs, rhs in
            if lhs.0.path != rhs.0.path {
                return lhs.0.path < rhs.0.path
            }
            let movingRight = lhs.0.to > lhs.1 || rhs.0.to > rhs.1
            return movingRight ? lhs.1 > rhs.1 : lhs.1 < rhs.1
        }
        for (move, current) in ordered {
            document.core.moveKeyframe(entityID: row.layerID, path: move.path, from: current, to: move.to)
            dragCurrentFrames[dragKey(path: move.path, origin: move.from)] = move.to
            didDrag = true
        }
    }

    private func endInteractiveDrag() {
        let editName = dragEditName
        let moved = didDrag
        dragSession = nil
        dragFrameOffset = 0
        dragCurrentFrames.removeAll()
        didDrag = false
        dragEditName = nil
        onTimeRangeDraggingChanged?(false)
        document.core.endDrag()
        if moved, let editName {
            registerEdit(editName)
        }
    }

    private func dragKey(path: String, origin: Int64) -> String {
        "\(path)#\(origin)"
    }

    /// Reposition segment lines + diamonds from live document frames without recreating views.
    private func syncKeyframeTrackGeometry(path: String) {
        let keyframes = document.core.keyframes(entityID: row.layerID, path: path).sorted { $0.frame < $1.frame }
        guard diamondViews.count == keyframes.count,
              segmentViews.count == max(keyframes.count - 1, 0)
        else {
            return
        }
        for (index, segmentView) in segmentViews.enumerated() {
            let start = keyframes[index].frame
            let end = keyframes[index + 1].frame
            let startX = contentX(for: start)
            let endX = contentX(for: end)
            segmentView.frame = CGRect(x: startX, y: 0, width: max(endX - startX, 2), height: propertyRowHeight)
            segmentView.accessibilityIdentifier = "\(start):\(end):\(path)"
        }
        for (index, diamond) in diamondViews.enumerated() {
            diamond.center = CGPoint(x: contentX(for: keyframes[index].frame), y: propertyRowHeight / 2)
        }
    }

    @objc private func handleSegmentTap(_ recognizer: UITapGestureRecognizer) {
        guard case let .keyframeTrack(path, _) = row.kind,
              let identifier = recognizer.view?.accessibilityIdentifier
        else {
            return
        }
        let parts = identifier.split(separator: ":", maxSplits: 2, omittingEmptySubsequences: false)
        guard parts.count == 3,
              let start = Int64(parts[0]),
              let end = Int64(parts[1])
        else {
            return
        }
        editorState.selectedLayerID = row.layerID
        editorState.selectedTimelineProperty = TimelinePropertySelection(layerID: row.layerID, path: path)
        editorState.selectedTimelineSegment = TimelineSegmentSelection(layerID: row.layerID,
                                                                       path: path,
                                                                       startFrame: start,
                                                                       endFrame: end)
        let keyframes = document.core.keyframes(entityID: row.layerID, path: path)
        guard let startKeyframe = keyframes.first(where: { $0.frame == start }) else {
            return
        }
        onPresentEasing?(TimelineEasingPresentationRequest(
            easing: startKeyframe.easing,
            easingAffectsPlayback: true,
            sourceView: recognizer.view ?? self,
            onSetEasing: { easing in
                self.document.core.setEasing(entityID: self.row.layerID, path: path, frame: start, easing: easing)
            },
            onDelete: nil,
            onCommit: { self.registerEdit("Set Easing") },
            onDragBegan: { self.document.core.beginDrag() },
            onDragEnded: {
                self.document.core.endDrag()
                self.registerEdit("Set Easing")
            },
        ))
    }
}

// MARK: - Bars / diamonds / segment badges

@MainActor
private final class TimelineKeyframeSegmentView: UIView {
    private let lineView = UIView()
    private let badgeBackground = UIView()
    private let badgeIcon = UIImageView()
    private var lineHeightConstraint: NSLayoutConstraint?

    override init(frame: CGRect) {
        super.init(frame: frame)
        lineView.translatesAutoresizingMaskIntoConstraints = false
        badgeBackground.translatesAutoresizingMaskIntoConstraints = false
        badgeIcon.translatesAutoresizingMaskIntoConstraints = false
        badgeBackground.layer.cornerRadius = 5
        badgeBackground.layer.borderWidth = 1
        badgeIcon.contentMode = .scaleAspectFit
        badgeIcon.preferredSymbolConfiguration = UIImage.SymbolConfiguration(pointSize: 9, weight: .semibold)
        addSubview(lineView)
        addSubview(badgeBackground)
        badgeBackground.addSubview(badgeIcon)
        let height = lineView.heightAnchor.constraint(equalToConstant: 2)
        lineHeightConstraint = height
        NSLayoutConstraint.activate([
            lineView.leadingAnchor.constraint(equalTo: leadingAnchor),
            lineView.trailingAnchor.constraint(equalTo: trailingAnchor),
            lineView.centerYAnchor.constraint(equalTo: centerYAnchor),
            height,
            badgeBackground.centerXAnchor.constraint(equalTo: centerXAnchor),
            badgeBackground.centerYAnchor.constraint(equalTo: centerYAnchor),
            badgeBackground.widthAnchor.constraint(equalToConstant: 18),
            badgeBackground.heightAnchor.constraint(equalToConstant: 16),
            badgeIcon.centerXAnchor.constraint(equalTo: badgeBackground.centerXAnchor),
            badgeIcon.centerYAnchor.constraint(equalTo: badgeBackground.centerYAnchor),
        ])
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    func configure(easing: EasingInfo, isSelected: Bool) {
        let lineHeight: CGFloat = isSelected ? 4 : 2
        lineView.backgroundColor = isSelected ? .tintColor : UIColor.secondaryLabel.withAlphaComponent(0.25)
        lineView.layer.cornerRadius = lineHeight / 2
        lineHeightConstraint?.constant = lineHeight

        let symbol = easing.kind == .HOLD ? "pause.fill" : "point.topleft.down.curvedto.point.bottomright.up"
        badgeIcon.image = UIImage(systemName: symbol)
        badgeIcon.tintColor = isSelected ? .white : UIColor.secondaryLabel.withAlphaComponent(0.7)
        badgeBackground.backgroundColor = isSelected ? .tintColor : UIColor.secondaryLabel.withAlphaComponent(0.08)
        badgeBackground.layer.borderColor = (isSelected
            ? UIColor.tintColor.withAlphaComponent(0.35)
            : UIColor.secondaryLabel.withAlphaComponent(0.22)).cgColor
    }
}

@MainActor
private final class TimelineTimeRangeBarView: UIView {
    let leadingHandle = UIView()
    let trailingHandle = UIView()
    private let fill = UIView()
    private let border = UIView()
    private let leadingGrip = UIView()
    private let trailingGrip = UIView()

    override init(frame: CGRect) {
        super.init(frame: frame)
        layer.cornerRadius = 4
        clipsToBounds = true
        fill.layer.cornerRadius = 4
        addSubview(fill)
        addSubview(leadingGrip)
        addSubview(trailingGrip)
        addSubview(leadingHandle)
        addSubview(trailingHandle)
        leadingHandle.backgroundColor = .clear
        trailingHandle.backgroundColor = .clear
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        fill.frame = bounds
        leadingGrip.frame = CGRect(x: 6, y: (bounds.height - (layerRowHeight - 14)) / 2, width: 2, height: layerRowHeight - 14)
        trailingGrip.frame = CGRect(x: bounds.width - 8, y: leadingGrip.frame.minY, width: 2, height: leadingGrip.frame.height)
        leadingHandle.frame = CGRect(x: 0, y: 0, width: 28, height: bounds.height)
        trailingHandle.frame = CGRect(x: bounds.width - 28, y: 0, width: 28, height: bounds.height)
    }

    func configure(isSelected: Bool) {
        fill.backgroundColor = isSelected ? UIColor.tintColor.withAlphaComponent(0.9) : UIColor.secondaryLabel.withAlphaComponent(0.08)
        layer.borderWidth = 1
        layer.borderColor = (isSelected ? UIColor.white.withAlphaComponent(0.65) : UIColor.secondaryLabel.withAlphaComponent(0.14)).cgColor
        leadingGrip.backgroundColor = isSelected ? UIColor.white.withAlphaComponent(0.9) : UIColor.secondaryLabel.withAlphaComponent(0.28)
        trailingGrip.backgroundColor = leadingGrip.backgroundColor
        leadingGrip.layer.cornerRadius = 1.5
        trailingGrip.layer.cornerRadius = 1.5
    }
}

@MainActor
private final class TimelinePropertyBarView: UIView {
    var onTap: (() -> Void)?
    let leadingHandle = UIView()
    let trailingHandle = UIView()
    private let label = UILabel()
    private let leadingGrip = UIView()
    private let trailingGrip = UIView()

    override init(frame: CGRect) {
        super.init(frame: frame)
        layer.cornerRadius = 4
        clipsToBounds = true
        label.font = .systemFont(ofSize: 9, weight: .semibold)
        label.textAlignment = .center
        label.lineBreakMode = .byTruncatingTail
        addSubview(label)
        addSubview(leadingGrip)
        addSubview(trailingGrip)
        addSubview(leadingHandle)
        addSubview(trailingHandle)
        leadingHandle.backgroundColor = .clear
        trailingHandle.backgroundColor = .clear
        let tap = UITapGestureRecognizer(target: self, action: #selector(handleTap))
        addGestureRecognizer(tap)
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        label.frame = bounds.insetBy(dx: 4, dy: 0)
        leadingGrip.frame = CGRect(x: 5, y: (bounds.height - 9) / 2, width: 2, height: 9)
        trailingGrip.frame = CGRect(x: bounds.width - 7, y: leadingGrip.frame.minY, width: 2, height: 9)
        leadingHandle.frame = CGRect(x: 0, y: 0, width: 24, height: bounds.height)
        trailingHandle.frame = CGRect(x: bounds.width - 24, y: 0, width: 24, height: bounds.height)
    }

    func configure(label text: String, isSelected: Bool) {
        label.text = text
        label.textColor = isSelected ? UIColor.white.withAlphaComponent(0.92) : .secondaryLabel
        backgroundColor = isSelected ? UIColor.tintColor.withAlphaComponent(0.9) : .clear
        layer.borderWidth = 1
        layer.borderColor = (isSelected ? UIColor.white.withAlphaComponent(0.7) : UIColor.secondaryLabel.withAlphaComponent(0.35)).cgColor
        leadingGrip.backgroundColor = isSelected ? UIColor.white.withAlphaComponent(0.9) : UIColor.secondaryLabel.withAlphaComponent(0.35)
        trailingGrip.backgroundColor = leadingGrip.backgroundColor
        leadingGrip.layer.cornerRadius = 1.5
        trailingGrip.layer.cornerRadius = 1.5
    }

    @objc private func handleTap() {
        onTap?()
    }
}

@MainActor
private final class TimelineKeyframeDiamondView: UIView {
    var onMoved: ((CGFloat) -> Void)?
    var onMoveEnded: (() -> Void)?
    var onDelete: (() -> Void)?
    var onSelect: (() -> Void)?

    private let imageView = UIImageView(image: UIImage(systemName: "diamond.fill"))
    private var didBeginDrag = false

    override init(frame _: CGRect) {
        super.init(frame: CGRect(x: 0, y: 0, width: 20, height: propertyRowHeight))
        imageView.translatesAutoresizingMaskIntoConstraints = false
        imageView.contentMode = .scaleAspectFit
        addSubview(imageView)
        NSLayoutConstraint.activate([
            imageView.centerXAnchor.constraint(equalTo: centerXAnchor),
            imageView.centerYAnchor.constraint(equalTo: centerYAnchor),
            imageView.widthAnchor.constraint(equalToConstant: 12),
            imageView.heightAnchor.constraint(equalToConstant: 12),
        ])
        let pan = UIPanGestureRecognizer(target: self, action: #selector(handlePan(_:)))
        addGestureRecognizer(pan)
        let tap = UITapGestureRecognizer(target: self, action: #selector(handleTap))
        addGestureRecognizer(tap)
        let interaction = UIContextMenuInteraction(delegate: self)
        addInteraction(interaction)
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    func configure(selected: Bool) {
        // Unselected uses secondaryLabel — white was invisible on light track chrome.
        imageView.tintColor = selected ? .tintColor : .secondaryLabel
    }

    @objc private func handleTap() {
        onSelect?()
    }

    @objc private func handlePan(_ recognizer: UIPanGestureRecognizer) {
        switch recognizer.state {
        case .began:
            didBeginDrag = false
        case .changed:
            let width = recognizer.translation(in: superview).x
            if !didBeginDrag, abs(width) >= 4 {
                didBeginDrag = true
            }
            guard didBeginDrag else {
                return
            }
            onMoved?(width)
        case .ended, .cancelled, .failed:
            if didBeginDrag {
                onMoveEnded?()
            }
            didBeginDrag = false
        default:
            break
        }
    }
}

extension TimelineKeyframeDiamondView: UIContextMenuInteractionDelegate {
    func contextMenuInteraction(_: UIContextMenuInteraction,
                                configurationForMenuAtLocation _: CGPoint) -> UIContextMenuConfiguration?
    {
        UIContextMenuConfiguration(identifier: nil, previewProvider: nil) { [weak self] _ in
            UIMenu(children: [
                UIAction(title: "Delete", attributes: .destructive) { _ in
                    self?.onDelete?()
                },
            ])
        }
    }
}
