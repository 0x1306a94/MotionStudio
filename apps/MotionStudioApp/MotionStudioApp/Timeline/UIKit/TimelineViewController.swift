//
//  TimelineViewController.swift
//  MotionStudioApp
//
//  UIKit timeline root. Replaces SwiftUI Timeline hosting in the editor shell.
//

import UIKit

@MainActor
final class TimelineViewController: UIViewController {
    private let document: MotionProjectState
    private let editorState: EditorState
    private let playheadClock: PlayheadClock
    private let performEdit: (String, () -> Void) -> Void
    private let registerEdit: (String) -> Void
    private let clearSelection: () -> Void

    private let scrollCoordinator: TimelineScrollCoordinator
    private let controlsView: TimelineControlsView
    private let sidebarView: TimelineSidebarView
    private let tracksView: TimelineTracksView
    private let rulerView = TimelineRulerCanvasView()
    private let playheadView = TimelinePlayheadView()
    private let layersHeaderLabel = UILabel()
    private let headerSplit = UIView()
    private let bodySplit = UIView()
    private let trackColumn = UIView()
    private let leftColumn = UIView()
    private let splitHitArea = UIView()
    private lazy var pointerOverlay = TimelinePointerInputOverlay(editorState: editorState)

    private var playheadListenerID: UUID?
    private var layerColumnWidthConstraint: NSLayoutConstraint?
    private var timelineRows: [TimelineRow] = []
    private var isObservingDocument = false
    private var isObservingPlayback = false
    private var isObservingZoom = false
    private var isObservingSelection = false
    private var isTimeRangeDragging = false
    private var splitDragStartWidth: CGFloat?
    private var horizontalPanStartScrollX: CGFloat?
    private var easingHost: UIViewController?

    init(document: MotionProjectState,
         editorState: EditorState,
         playheadClock: PlayheadClock,
         perform: @escaping (String, () -> Void) -> Void,
         registerEdit: @escaping (String) -> Void,
         clearSelection: @escaping () -> Void)
    {
        self.document = document
        self.editorState = editorState
        self.playheadClock = playheadClock
        performEdit = perform
        self.registerEdit = registerEdit
        self.clearSelection = clearSelection
        scrollCoordinator = TimelineScrollCoordinator(editorState: editorState, playheadClock: playheadClock)
        controlsView = TimelineControlsView(editorState: editorState)
        sidebarView = TimelineSidebarView(document: document,
                                          editorState: editorState,
                                          playheadClock: playheadClock,
                                          perform: perform,
                                          registerEdit: registerEdit,
                                          clearSelection: clearSelection)
        tracksView = TimelineTracksView(document: document,
                                        editorState: editorState,
                                        perform: perform,
                                        registerEdit: registerEdit)
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = UIColor.secondarySystemBackground.withAlphaComponent(0.35)
        buildHierarchy()
        wireActions()
        reloadFromDocument()
        beginObservationLoops()
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        let trackWidth = max(1, trackColumn.bounds.width - trackLeadingInset * 2)
        if abs(trackWidth - scrollCoordinator.trackViewportWidth) > 0.5 {
            scrollCoordinator.updateTrackViewportWidth(trackWidth)
            refreshTrackChrome(updateControls: true)
        } else {
            updatePlayheadPosition()
        }
    }

    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear(animated)
        if playheadListenerID == nil {
            playheadListenerID = playheadClock.addListener { [weak self] frame in
                self?.handlePlayheadFrameChanged(frame)
            }
        }
        reloadFromDocument()
    }

    override func viewWillDisappear(_ animated: Bool) {
        super.viewWillDisappear(animated)
        if let playheadListenerID {
            playheadClock.removeListener(playheadListenerID)
            self.playheadListenerID = nil
        }
    }

    private func buildHierarchy() {
        controlsView.translatesAutoresizingMaskIntoConstraints = false
        leftColumn.translatesAutoresizingMaskIntoConstraints = false
        trackColumn.translatesAutoresizingMaskIntoConstraints = false
        layersHeaderLabel.translatesAutoresizingMaskIntoConstraints = false
        headerSplit.translatesAutoresizingMaskIntoConstraints = false
        bodySplit.translatesAutoresizingMaskIntoConstraints = false

        layersHeaderLabel.text = "Layers"
        layersHeaderLabel.font = .preferredFont(forTextStyle: .caption1)
        layersHeaderLabel.textColor = .secondaryLabel

        styleSplit(headerSplit)
        styleSplit(bodySplit)
        sidebarView.delegate = self
        tracksView.delegate = self
        splitHitArea.translatesAutoresizingMaskIntoConstraints = false
        splitHitArea.backgroundColor = .clear

        let headerSeparator = hairline()
        let bodySeparator = hairline()
        let controlsSeparator = hairline()

        view.addSubview(controlsView)
        view.addSubview(controlsSeparator)
        view.addSubview(leftColumn)
        view.addSubview(headerSplit)
        view.addSubview(bodySplit)
        view.addSubview(trackColumn)
        view.addSubview(splitHitArea)

        leftColumn.addSubview(layersHeaderLabel)
        leftColumn.addSubview(headerSeparator)
        leftColumn.addSubview(sidebarView)

        trackColumn.addSubview(rulerView)
        trackColumn.addSubview(bodySeparator)
        trackColumn.addSubview(tracksView)
        trackColumn.addSubview(playheadView)
        trackColumn.addSubview(pointerOverlay)
        trackColumn.clipsToBounds = true

        let widthConstraint = leftColumn.widthAnchor.constraint(equalToConstant: scrollCoordinator.layerColumnWidth)
        layerColumnWidthConstraint = widthConstraint

        NSLayoutConstraint.activate([
            controlsView.topAnchor.constraint(equalTo: view.topAnchor),
            controlsView.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            controlsView.trailingAnchor.constraint(equalTo: view.trailingAnchor),

            controlsSeparator.topAnchor.constraint(equalTo: controlsView.bottomAnchor),
            controlsSeparator.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            controlsSeparator.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            controlsSeparator.heightAnchor.constraint(equalToConstant: 1 / UIScreen.main.scale),

            leftColumn.topAnchor.constraint(equalTo: controlsSeparator.bottomAnchor),
            leftColumn.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            leftColumn.bottomAnchor.constraint(equalTo: view.bottomAnchor),
            widthConstraint,

            headerSplit.topAnchor.constraint(equalTo: leftColumn.topAnchor),
            headerSplit.leadingAnchor.constraint(equalTo: leftColumn.trailingAnchor),
            headerSplit.widthAnchor.constraint(equalToConstant: splitDividerWidth),
            headerSplit.heightAnchor.constraint(equalToConstant: rulerHeight),

            bodySplit.topAnchor.constraint(equalTo: headerSplit.bottomAnchor),
            bodySplit.leadingAnchor.constraint(equalTo: leftColumn.trailingAnchor),
            bodySplit.widthAnchor.constraint(equalToConstant: splitDividerWidth),
            bodySplit.bottomAnchor.constraint(equalTo: view.bottomAnchor),

            trackColumn.topAnchor.constraint(equalTo: leftColumn.topAnchor),
            trackColumn.leadingAnchor.constraint(equalTo: headerSplit.trailingAnchor),
            trackColumn.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            trackColumn.bottomAnchor.constraint(equalTo: view.bottomAnchor),

            layersHeaderLabel.topAnchor.constraint(equalTo: leftColumn.topAnchor),
            layersHeaderLabel.leadingAnchor.constraint(equalTo: leftColumn.leadingAnchor, constant: 8),
            layersHeaderLabel.trailingAnchor.constraint(equalTo: leftColumn.trailingAnchor, constant: -8),
            layersHeaderLabel.heightAnchor.constraint(equalToConstant: rulerHeight),

            headerSeparator.topAnchor.constraint(equalTo: layersHeaderLabel.bottomAnchor),
            headerSeparator.leadingAnchor.constraint(equalTo: leftColumn.leadingAnchor),
            headerSeparator.trailingAnchor.constraint(equalTo: leftColumn.trailingAnchor),
            headerSeparator.heightAnchor.constraint(equalToConstant: 1 / UIScreen.main.scale),

            sidebarView.topAnchor.constraint(equalTo: headerSeparator.bottomAnchor),
            sidebarView.leadingAnchor.constraint(equalTo: leftColumn.leadingAnchor),
            sidebarView.trailingAnchor.constraint(equalTo: leftColumn.trailingAnchor),
            sidebarView.bottomAnchor.constraint(equalTo: leftColumn.bottomAnchor),

            rulerView.topAnchor.constraint(equalTo: trackColumn.topAnchor),
            rulerView.leadingAnchor.constraint(equalTo: trackColumn.leadingAnchor),
            rulerView.trailingAnchor.constraint(equalTo: trackColumn.trailingAnchor),
            rulerView.heightAnchor.constraint(equalToConstant: rulerHeight),

            bodySeparator.topAnchor.constraint(equalTo: rulerView.bottomAnchor),
            bodySeparator.leadingAnchor.constraint(equalTo: trackColumn.leadingAnchor),
            bodySeparator.trailingAnchor.constraint(equalTo: trackColumn.trailingAnchor),
            bodySeparator.heightAnchor.constraint(equalToConstant: 1 / UIScreen.main.scale),

            tracksView.topAnchor.constraint(equalTo: bodySeparator.bottomAnchor),
            tracksView.leadingAnchor.constraint(equalTo: trackColumn.leadingAnchor),
            tracksView.trailingAnchor.constraint(equalTo: trackColumn.trailingAnchor),
            tracksView.bottomAnchor.constraint(equalTo: trackColumn.bottomAnchor),

            playheadView.topAnchor.constraint(equalTo: trackColumn.topAnchor),
            playheadView.leadingAnchor.constraint(equalTo: trackColumn.leadingAnchor),
            playheadView.trailingAnchor.constraint(equalTo: trackColumn.trailingAnchor),
            playheadView.bottomAnchor.constraint(equalTo: trackColumn.bottomAnchor),

            pointerOverlay.topAnchor.constraint(equalTo: trackColumn.topAnchor),
            pointerOverlay.leadingAnchor.constraint(equalTo: trackColumn.leadingAnchor),
            pointerOverlay.trailingAnchor.constraint(equalTo: trackColumn.trailingAnchor),
            pointerOverlay.bottomAnchor.constraint(equalTo: trackColumn.bottomAnchor),

            splitHitArea.topAnchor.constraint(equalTo: leftColumn.topAnchor),
            splitHitArea.leadingAnchor.constraint(equalTo: leftColumn.trailingAnchor),
            splitHitArea.widthAnchor.constraint(equalToConstant: splitDividerWidth),
            splitHitArea.bottomAnchor.constraint(equalTo: leftColumn.bottomAnchor),
        ])
    }

    private func wireActions() {
        controlsView.onZoomChanged = { [weak self] in
            guard let self else {
                return
            }
            scrollCoordinator.noteExternalZoomChange()
            refreshTrackChrome(updateControls: true)
        }
        rulerView.onScrub = { [weak self] visibleX in
            self?.scrub(atVisibleX: visibleX)
        }
        playheadView.onScrub = { [weak self] visibleX in
            self?.scrub(atVisibleX: visibleX)
        }
        pointerOverlay.onPlayheadHoveringChanged = { [weak self] hovering in
            self?.playheadView.setHovering(hovering)
        }
        tracksView.onPresentEasing = { [weak self] request in
            self?.presentEasingEditor(request)
        }

        let splitPan = UIPanGestureRecognizer(target: self, action: #selector(handleSplitDrag(_:)))
        splitHitArea.addGestureRecognizer(splitPan)

        let horizontalPan = UIPanGestureRecognizer(target: self, action: #selector(handleHorizontalPan(_:)))
        horizontalPan.delegate = self
        tracksView.addGestureRecognizer(horizontalPan)
    }

    private func reloadFromDocument() {
        let core = document.core
        let compositionID = core.firstCompositionID
        scrollCoordinator.duration = core.duration(compositionID: compositionID)
        let frameRate = core.frameRate(compositionID: compositionID)
        let layerIDs = Array(core.layerIDs(compositionID: compositionID).reversed())
        timelineRows = buildTimelineRows(core: core, layerIDs: layerIDs)
        sidebarView.reloadRows(timelineRows)
        tracksView.reload(rows: timelineRows,
                          duration: scrollCoordinator.duration,
                          pointsPerFrame: scrollCoordinator.pointsPerFrame,
                          scrollX: scrollCoordinator.scrollX)
        controlsView.reload(duration: scrollCoordinator.duration, frame: playheadClock.frame)
        refreshTrackChrome(updateControls: true, frameRate: frameRate)
    }

    private func refreshTrackChrome(updateControls: Bool, frameRate: Double? = nil) {
        let rate = frameRate ?? document.core.frameRate(compositionID: document.core.firstCompositionID)
        let pointsPerFrame = scrollCoordinator.pointsPerFrame
        let scrollX = scrollCoordinator.scrollX
        rulerView.update(duration: scrollCoordinator.duration,
                         frameRate: rate,
                         pointsPerFrame: pointsPerFrame,
                         scrollX: scrollX)
        tracksView.updateHorizontalMetrics(pointsPerFrame: pointsPerFrame, scrollX: scrollX)
        if updateControls {
            controlsView.refreshPlaybackState()
        }
        updatePlayheadPosition()
    }

    private func updatePlayheadPosition() {
        let visibleX = timelineX(for: playheadClock.frame, pointsPerFrame: scrollCoordinator.pointsPerFrame)
            - scrollCoordinator.scrollX
        let contentX: CGFloat?
        if visibleX >= 0, visibleX <= scrollCoordinator.trackViewportWidth {
            contentX = trackLeadingInset + visibleX
            playheadView.setContentX(contentX)
        } else {
            contentX = nil
            playheadView.setContentX(nil)
        }
        pointerOverlay.update(duration: scrollCoordinator.duration,
                              pointsPerFrame: scrollCoordinator.pointsPerFrame,
                              trackWidth: scrollCoordinator.trackWidth,
                              viewportWidth: scrollCoordinator.trackViewportWidth,
                              visiblePlayheadX: contentX ?? -1000)
    }

    private func handlePlayheadFrameChanged(_ frame: Int64) {
        controlsView.setPlayheadFrame(frame)
        updatePlayheadPosition()
        sidebarView.updatePlayheadBadges()
    }

    private func scrub(atVisibleX visibleX: CGFloat) {
        playheadClock.publish(scrollCoordinator.frame(atVisibleX: visibleX))
    }

    private func beginObservationLoops() {
        observeDocumentRevision()
        observePlaybackState()
        observeZoomState()
        observeSelectionState()
    }

    private func observeDocumentRevision() {
        isObservingDocument = true
        withObservationTracking {
            _ = document.core.revision
        } onChange: { [weak self] in
            Task { @MainActor [weak self] in
                guard let self, isObservingDocument else {
                    return
                }
                reloadFromDocument()
                observeDocumentRevision()
            }
        }
    }

    private func observePlaybackState() {
        isObservingPlayback = true
        withObservationTracking {
            _ = editorState.isPlaying
            _ = editorState.previewBackdrop
        } onChange: { [weak self] in
            Task { @MainActor [weak self] in
                guard let self, isObservingPlayback else {
                    return
                }
                controlsView.refreshPlaybackState()
                observePlaybackState()
            }
        }
    }

    private func observeZoomState() {
        isObservingZoom = true
        withObservationTracking {
            _ = editorState.timelinePointsPerFrame
            _ = editorState.timelineScrollX
        } onChange: { [weak self] in
            Task { @MainActor [weak self] in
                guard let self, isObservingZoom else {
                    return
                }
                scrollCoordinator.clampScroll()
                refreshTrackChrome(updateControls: true)
                observeZoomState()
            }
        }
    }

    private func observeSelectionState() {
        isObservingSelection = true
        withObservationTracking {
            _ = editorState.selectedLayerIDs
            _ = editorState.selectedTimelineProperty
        } onChange: { [weak self] in
            Task { @MainActor [weak self] in
                guard let self, isObservingSelection else {
                    return
                }
                sidebarView.refreshSelectionAppearance()
                tracksView.refreshSelectionAppearance()
                observeSelectionState()
            }
        }
    }

    private func hairline() -> UIView {
        let line = UIView()
        line.translatesAutoresizingMaskIntoConstraints = false
        line.backgroundColor = UIColor.separator
        return line
    }
}

extension TimelineViewController: TimelineSidebarViewDelegate, TimelineTracksViewDelegate {
    func timelineSidebarDidScroll(_: TimelineSidebarView, offsetY: CGFloat) {
        tracksView.contentOffsetY = offsetY
    }

    func timelineTracksDidScroll(_: TimelineTracksView, offsetY: CGFloat) {
        sidebarView.contentOffsetY = offsetY
    }

    func timelineTracksTimeRangeDraggingChanged(_: TimelineTracksView, isDragging: Bool) {
        isTimeRangeDragging = isDragging
    }
}

extension TimelineViewController: UIGestureRecognizerDelegate {
    @objc private func handleSplitDrag(_ recognizer: UIPanGestureRecognizer) {
        switch recognizer.state {
        case .began:
            splitDragStartWidth = scrollCoordinator.layerColumnWidth
            styleSplit(headerSplit, active: true)
            styleSplit(bodySplit, active: true)
        case .changed:
            guard let start = splitDragStartWidth else {
                return
            }
            let next = min(max(start + recognizer.translation(in: view).x, minLayerColumnWidth), maxLayerColumnWidth)
            scrollCoordinator.layerColumnWidth = next
            layerColumnWidthConstraint?.constant = next
            view.layoutIfNeeded()
            scrollCoordinator.updateTrackViewportWidth(max(1, trackColumn.bounds.width - trackLeadingInset * 2))
            refreshTrackChrome(updateControls: false)
        case .ended, .cancelled, .failed:
            splitDragStartWidth = nil
            styleSplit(headerSplit, active: false)
            styleSplit(bodySplit, active: false)
        default:
            break
        }
    }

    @objc private func handleHorizontalPan(_ recognizer: UIPanGestureRecognizer) {
        guard !isTimeRangeDragging else {
            return
        }
        switch recognizer.state {
        case .began:
            let playheadX = scrollCoordinator.visibleContentX(for: playheadClock.frame)
            let startX = recognizer.location(in: tracksView).x
            if abs(startX - playheadX) <= 10 {
                horizontalPanStartScrollX = .nan
            } else {
                horizontalPanStartScrollX = CGFloat(editorState.timelineScrollX)
            }
        case .changed:
            guard let start = horizontalPanStartScrollX, !start.isNaN else {
                return
            }
            let next = start - recognizer.translation(in: tracksView).x
            editorState.timelineScrollX = Double(min(max(next, 0),
                                                     max(0, scrollCoordinator.trackWidth - scrollCoordinator.trackViewportWidth)))
        default:
            horizontalPanStartScrollX = nil
        }
    }

    func gestureRecognizerShouldBegin(_ gestureRecognizer: UIGestureRecognizer) -> Bool {
        guard let pan = gestureRecognizer as? UIPanGestureRecognizer, pan.view === tracksView else {
            return true
        }
        let velocity = pan.velocity(in: tracksView)
        return abs(velocity.x) > abs(velocity.y)
    }

    func gestureRecognizer(_: UIGestureRecognizer,
                           shouldRecognizeSimultaneouslyWith _: UIGestureRecognizer) -> Bool
    {
        true
    }

    private func styleSplit(_ view: UIView, active: Bool = false) {
        view.backgroundColor = active
            ? UIColor.tintColor.withAlphaComponent(0.35)
            : UIColor.secondaryLabel.withAlphaComponent(0.2)
    }

    private func presentEasingEditor(_ request: TimelineEasingPresentationRequest) {
        easingHost?.dismiss(animated: false)
        let host = TimelineEasingPopoverController(easing: request.easing,
                                                   easingAffectsPlayback: request.easingAffectsPlayback,
                                                   onSetEasing: request.onSetEasing,
                                                   onDelete: request.onDelete,
                                                   onCommit: request.onCommit,
                                                   onDragBegan: request.onDragBegan,
                                                   onDragEnded: request.onDragEnded)
        if let pop = host.popoverPresentationController {
            pop.sourceView = request.sourceView
            pop.sourceRect = request.sourceView.bounds
            pop.permittedArrowDirections = [.up, .down]
            pop.delegate = self
        }
        easingHost = host
        present(host, animated: true)
    }
}

extension TimelineViewController: UIPopoverPresentationControllerDelegate {
    func adaptivePresentationStyle(for _: UIPresentationController) -> UIModalPresentationStyle {
        .none
    }
}
