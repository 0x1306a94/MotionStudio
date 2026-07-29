//
//  TimelineTracksView.swift
//  MotionStudioApp
//
//  Layer time-range bars, property spans, and keyframe tracks (UIKit).
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

    func reload(rows: [TimelineRow], duration: Int64, pointsPerFrame: CGFloat, scrollX: CGFloat) {
        self.rows = rows
        self.duration = duration
        self.pointsPerFrame = pointsPerFrame
        self.scrollX = scrollX
        rebuildRows()
    }

    func updateHorizontalMetrics(pointsPerFrame: CGFloat, scrollX: CGFloat) {
        self.pointsPerFrame = pointsPerFrame
        self.scrollX = scrollX
        for rowView in rowViews {
            rowView.updateMetrics(pointsPerFrame: pointsPerFrame, scrollX: scrollX, duration: duration)
        }
    }

    func refreshSelectionAppearance() {
        for rowView in rowViews {
            rowView.refreshSelection()
        }
    }

    private func configureScroll() {
        scrollView.translatesAutoresizingMaskIntoConstraints = false
        scrollView.delegate = self
        scrollView.alwaysBounceVertical = true
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
                delegate?.timelineTracksTimeRangeDraggingChanged(self, isDragging: isDragging)
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

@MainActor
private final class TimelineTrackRowView: UIView {
    var onTimeRangeDraggingChanged: ((Bool) -> Void)?
    var onPresentEasing: ((TimelineEasingPresentationRequest) -> Void)?

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

    private var dragStartRange: TimeRangeDraft?
    private var dragFrameOffset: Int64 = 0
    private var didDragRange = false
    private var activeDragEdge: TimeRangeDragEdge?

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

    private func reloadContent() {
        segmentViews.forEach { $0.removeFromSuperview() }
        segmentViews.removeAll()
        diamondViews.forEach { $0.removeFromSuperview() }
        diamondViews.removeAll()
        timeRangeBar.isHidden = true
        propertyBar.isHidden = true

        switch row.kind {
        case .layer:
            layoutTimeRange()
        case let .propertySpan(path, label):
            layoutPropertySpan(path: path, label: label)
        case let .keyframeTrack(path, _):
            layoutKeyframeTrack(path: path)
        }
    }

    private func layoutTimeRange() {
        let core = document.core
        let paths = timelineAnimatedPropertyPaths(core: core, layerID: row.layerID)
        guard let range = keyframeRange(paths: paths) else {
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
            let line = UIView(frame: CGRect(x: startX, y: propertyRowHeight / 2 - 1, width: width, height: selected ? 4 : 2))
            line.backgroundColor = selected ? .tintColor : UIColor.secondaryLabel.withAlphaComponent(0.25)
            line.layer.cornerRadius = line.bounds.height / 2
            let tap = UITapGestureRecognizer(target: self, action: #selector(handleSegmentTap(_:)))
            line.addGestureRecognizer(tap)
            line.isUserInteractionEnabled = true
            line.accessibilityIdentifier = "\(segment.start.frame):\(segment.end.frame):\(path)"
            addSubview(line)
            segmentViews.append(line)
        }
        for (index, keyframe) in keyframes.enumerated() {
            let selected = trackSelected || isKeyframeSelected(keyframe.frame, path: path)
            let diamond = TimelineKeyframeDiamondView()
            diamond.configure(selected: selected)
            diamond.center = CGPoint(x: contentX(for: keyframe.frame), y: propertyRowHeight / 2)
            var dragOriginFrame: Int64?
            var lastFrame = keyframe.frame
            diamond.onMoved = { [weak self] translationWidth in
                guard let self else {
                    return
                }
                if dragOriginFrame == nil {
                    dragOriginFrame = lastFrame
                    document.core.beginDrag()
                }
                guard let origin = dragOriginFrame else {
                    return
                }
                let target = min(max(Int64((CGFloat(origin) + translationWidth / pointsPerFrame).rounded()), 0),
                                 duration)
                if target != lastFrame {
                    document.core.moveKeyframe(entityID: row.layerID, path: path, from: lastFrame, to: target)
                    lastFrame = target
                    diamond.center = CGPoint(x: contentX(for: target), y: propertyRowHeight / 2)
                }
            }
            diamond.onMoveEnded = { [weak self] in
                guard let self else {
                    return
                }
                dragOriginFrame = nil
                document.core.endDrag()
                registerEdit("Move Keyframe")
            }
            diamond.onDelete = { [weak self] in
                guard let self else {
                    return
                }
                performEdit("Delete Keyframe") {
                    self.document.core.removeKeyframe(entityID: self.row.layerID, path: path, frame: lastFrame)
                }
            }
            diamond.onSelect = { [weak self] in
                guard let self else {
                    return
                }
                editorState.selectedLayerID = row.layerID
                editorState.selectedTimelineProperty = TimelinePropertySelection(layerID: row.layerID, path: path)
                let hasOutgoing = index + 1 < keyframes.count
                if hasOutgoing {
                    let next = keyframes[index + 1]
                    editorState.selectedTimelineSegment = TimelineSegmentSelection(layerID: row.layerID,
                                                                                   path: path,
                                                                                   startFrame: lastFrame,
                                                                                   endFrame: next.frame)
                } else {
                    editorState.selectedTimelineSegment = nil
                }
                onPresentEasing?(TimelineEasingPresentationRequest(
                    easing: keyframe.easing,
                    easingAffectsPlayback: hasOutgoing,
                    sourceView: diamond,
                    onSetEasing: { easing in
                        self.document.core.setEasing(entityID: self.row.layerID, path: path, frame: lastFrame, easing: easing)
                    },
                    onDelete: {
                        self.performEdit("Delete Keyframe") {
                            self.document.core.removeKeyframe(entityID: self.row.layerID, path: path, frame: lastFrame)
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
        let leading = UIPanGestureRecognizer(target: self, action: #selector(handleLeadingDrag(_:)))
        let trailing = UIPanGestureRecognizer(target: self, action: #selector(handleTrailingDrag(_:)))
        timeRangeBar.leadingHandle.addGestureRecognizer(leading)
        timeRangeBar.trailingHandle.addGestureRecognizer(trailing)
    }

    @objc private func selectLayerRow() {
        editorState.selectedLayerID = row.layerID
        editorState.selectedTimelineProperty = nil
        editorState.selectedTimelineSegment = nil
    }

    @objc private func handleLeadingDrag(_ recognizer: UIPanGestureRecognizer) {
        handleRangeDrag(edge: .leading, recognizer: recognizer)
    }

    @objc private func handleTrailingDrag(_ recognizer: UIPanGestureRecognizer) {
        handleRangeDrag(edge: .trailing, recognizer: recognizer)
    }

    private func handleRangeDrag(edge: TimeRangeDragEdge, recognizer: UIPanGestureRecognizer) {
        let paths = timelineAnimatedPropertyPaths(core: document.core, layerID: row.layerID)
        switch recognizer.state {
        case .began, .changed:
            onTimeRangeDraggingChanged?(true)
            let locationX = recognizer.location(in: self).x
            updateRangeDrag(edge: edge, paths: paths, locationX: locationX)
        case .ended, .cancelled, .failed:
            endRangeDrag()
        default:
            break
        }
    }

    private func updateRangeDrag(edge: TimeRangeDragEdge, paths: [String], locationX: CGFloat) {
        let pointerFrame = Int64(((locationX - trackLeadingInset + scrollX) / pointsPerFrame).rounded())
        if dragStartRange == nil {
            guard let range = keyframeRange(paths: paths) else {
                return
            }
            dragStartRange = range
            dragFrameOffset = pointerFrame - range.frame(for: edge)
            didDragRange = false
            activeDragEdge = edge
            selectLayerRow()
            document.core.beginDrag()
        }
        guard let startRange = dragStartRange,
              let currentRange = keyframeRange(paths: paths)
        else {
            return
        }
        let sourceFrame: Int64
        let targetFrame: Int64
        let draggedFrame = pointerFrame - dragFrameOffset
        switch edge {
        case .leading:
            sourceFrame = currentRange.startFrame
            targetFrame = min(max(draggedFrame, 0), startRange.leadingMaxFrame)
        case .trailing:
            sourceFrame = currentRange.endFrame
            targetFrame = min(max(draggedFrame, startRange.trailingMinFrame), duration)
        }
        guard sourceFrame != targetFrame else {
            return
        }
        for path in paths where document.core.keyframes(entityID: row.layerID, path: path).contains(where: { $0.frame == sourceFrame }) {
            document.core.moveKeyframe(entityID: row.layerID, path: path, from: sourceFrame, to: targetFrame)
        }
        didDragRange = true
    }

    private func endRangeDrag() {
        dragStartRange = nil
        dragFrameOffset = 0
        activeDragEdge = nil
        onTimeRangeDraggingChanged?(false)
        document.core.endDrag()
        if didDragRange {
            registerEdit("Move Time Range")
        }
        didDragRange = false
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

// MARK: - Bars / diamonds

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
    private var dragStartTranslation: CGFloat = 0
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
        imageView.tintColor = selected ? .tintColor : UIColor.white.withAlphaComponent(0.68)
    }

    @objc private func handleTap() {
        onSelect?()
    }

    @objc private func handlePan(_ recognizer: UIPanGestureRecognizer) {
        switch recognizer.state {
        case .began:
            didBeginDrag = false
            dragStartTranslation = 0
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
