//
//  CanvasViewController.swift
//  MotionStudioApp
//
//  UIKit live preview canvas. Owns the MTKView directly and keeps SwiftUI out
//  of the UIKit editor shell's canvas path.
//

import MetalKit
import MotionStudioBridging
import Observation
import QuartzCore
import UIKit

@MainActor
final class CanvasViewController: UIViewController, MTKViewDelegate {
    private let document: MotionProjectState
    private let editorState: EditorState
    private let clearSelection: () -> Void
    private let registerEdit: (String) -> Void

    private var metalView: MTKView!

    private nonisolated(unsafe) var canvas: OpaquePointer?
    private var compositionID: UInt64 = 0
    private var playheadFrame: Int64 = 0
    private var previewFrame: Double = 0
    private var duration: Int64 = 1
    private var previewBackdrop: MS_PREVIEWER_BACKDROP = .TRANSPARENT
    private var frameRate: Double = 30
    private var playbackFramesPerSecond: Double = 30
    private var isPlaying = false
    private var lastPlayheadPublishTime: CFTimeInterval = 0
    private var lastPlaybackTimingLogTime: CFTimeInterval = 0
    private var lastPlaybackDrawTime: CFTimeInterval?
    private var currentDrawGapMilliseconds: Double = 0
    private var currentAdvancedFrames: Int = 0
    private var currentSkippedFrames: Int = 0
    private var currentPublishMilliseconds: Double = 0
    private var profileStatsStartTime: CFTimeInterval = 0
    private var profileFrameCount: Int = 0
    private var profileDroppedFrameCount: Int = 0
    private var profileTotalRenderMilliseconds: Double = 0
    private var profileMaxRenderMilliseconds: Double = 0
    private var profileMaxDrawGapMilliseconds: Double = 0
    private var lastSyncTime: CFTimeInterval?
    private var lastDrawRequestTime: CFTimeInterval?
    private var lastDrawStartTime: CFTimeInterval?

    // User view transform on top of fit (zoom 1 = fit to drawable).
    private var canvasZoom: CGFloat = 1
    private var canvasPan: CGPoint = .zero
    /// Current unobscured area insets (panels/toolbar/timeline), queried each
    /// time the composition is re-fitted (initial display, double-tap reset).
    var viewportInsetsProvider: (() -> UIEdgeInsets)?
    private var viewDidAppearOnce = false
    private var didApplyInitialFit = false
    private var lastPinchScale: CGFloat = 1
    private var lastTouchPanTranslation: CGPoint = .zero
    private var lastScrollTranslation: CGPoint = .zero
    private var lastPointerLocation: CGPoint?
    private var pinchGesture: UIPinchGestureRecognizer?
    private var freeTransformDrag: FreeTransformDrag?
    private var freeTransformDidMove = false

    private static let minCanvasZoom: CGFloat = 0.02
    private static let maxCanvasZoom: CGFloat = 64
    private static let scrollZoomSensitivity: CGFloat = 0.01
    private static let hitTolerancePoints: CGFloat = 6
    private static let handleHitPoints: CGFloat = 14
    private static let rotateInnerPoints: CGFloat = 10
    private static let rotateOuterPoints: CGFloat = 36
    private static let viewportFitMargin: CGFloat = 12

    init(document: MotionProjectState,
         editorState: EditorState,
         clearSelection: @escaping () -> Void,
         registerEdit: @escaping (String) -> Void)
    {
        self.document = document
        self.editorState = editorState
        self.clearSelection = clearSelection
        self.registerEdit = registerEdit
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    deinit {
        if let canvas {
            ms_canvas_destroy(canvas)
        }
    }

    /// Stops continuous preview redraw. Must be called before the editor scene tears down,
    /// otherwise MTKView keeps drawing while the controller is retained.
    func stopPlayback() {
        let wasPlaying = isPlaying
        isPlaying = false
        guard isViewLoaded else { return }
        configurePlayback(false, wasPlaying: wasPlaying)
    }

    func shutdown() {
        stopPlayback()
        if isViewLoaded {
            metalView.delegate = nil
        }
        if let canvas {
            ms_canvas_destroy(canvas)
            self.canvas = nil
        }
    }

    override func loadView() {
        let view = MTKView()
        view.device = MTLCreateSystemDefaultDevice()
        view.isPaused = true
        view.enableSetNeedsDisplay = true
        view.framebufferOnly = true
        view.autoResizeDrawable = true
        // Multi-touch delivery is off by default; pinch and two-finger pan
        // never begin without it.
        view.isMultipleTouchEnabled = true
        view.delegate = self
        view.clearColor = MTLClearColor(red: 1.0, green: 1.0, blue: 1.0, alpha: 1.0)
        self.view = view
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .clear
        setupMetalView()
        configureCanvasGestures()
        syncFromState()
        observeStateChanges()
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        viewDidAppearOnce = true
        applyInitialFitIfNeeded()
    }

    private func setupMetalView() {
        metalView = MTKView(frame: view.bounds)
        metalView.device = MTLCreateSystemDefaultDevice()
        metalView.isPaused = true
        metalView.enableSetNeedsDisplay = true
        metalView.framebufferOnly = true
        metalView.autoResizeDrawable = true
        // Multi-touch delivery is off by default; pinch and two-finger pan
        // never begin without it.
        metalView.isMultipleTouchEnabled = true
        metalView.delegate = self
        metalView.isHidden = true
        metalView.autoresizingMask = [.flexibleWidth, .flexibleHeight]

        view.addSubview(metalView)
    }

    private func configureCanvasGestures() {
        let doubleTapGesture = UITapGestureRecognizer(target: self, action: #selector(handleCanvasDoubleTap))
        doubleTapGesture.numberOfTapsRequired = 2
        view.addGestureRecognizer(doubleTapGesture)

        let tapGesture = UITapGestureRecognizer(target: self, action: #selector(handleCanvasTap(_:)))
        view.addGestureRecognizer(tapGesture)

        let pinchGesture = UIPinchGestureRecognizer(target: self, action: #selector(handleCanvasPinch(_:)))
        self.pinchGesture = pinchGesture
        view.addGestureRecognizer(pinchGesture)

        // Scroll events carry no pointer location (WWDC20 session 10094), so
        // track the pointer separately to anchor Cmd+scroll zoom under it.
        let hoverGesture = UIHoverGestureRecognizer(target: self, action: #selector(handleCanvasHover(_:)))
        view.addGestureRecognizer(hoverGesture)

        let touchPanGesture = UIPanGestureRecognizer(target: self, action: #selector(handleCanvasTouchPan(_:)))
        touchPanGesture.minimumNumberOfTouches = 2
        touchPanGesture.maximumNumberOfTouches = 2
        view.addGestureRecognizer(touchPanGesture)

        let layerDragGesture = UIPanGestureRecognizer(target: self, action: #selector(handleLayerDrag(_:)))
        layerDragGesture.minimumNumberOfTouches = 1
        layerDragGesture.maximumNumberOfTouches = 1
        view.addGestureRecognizer(layerDragGesture)

        // Trackpad / mouse wheel input: UIKit routes scroll events to a
        // touchless pan recognizer (Catalyst scroll and iPad pointer alike).
        let scrollGesture = UIPanGestureRecognizer(target: self, action: #selector(handleCanvasScroll(_:)))
        scrollGesture.maximumNumberOfTouches = 0
        scrollGesture.allowedScrollTypesMask = [.continuous, .discrete]
        view.addGestureRecognizer(scrollGesture)
    }

    func syncFromState() {
        let core = document.core
        let revision = core.revision
        let compositionID = core.firstCompositionID
        let duration = core.duration(compositionID: compositionID)
        let frameRate = core.frameRate(compositionID: compositionID)
        sync(compositionID: compositionID,
             playheadFrame: editorState.playheadFrame,
             isPlaying: editorState.isPlaying,
             duration: duration,
             frameRate: frameRate,
             previewBackdrop: editorState.previewBackdrop,
             revision: revision)
    }

    private func observeStateChanges() {
        withObservationTracking {
            _ = document.core.revision
            _ = editorState.selectedLayerIDs
            _ = editorState.playheadFrame
            _ = editorState.isPlaying
            _ = editorState.previewBackdrop
        } onChange: { [weak self] in
            Task { @MainActor [weak self] in
                self?.syncFromState()
                self?.observeStateChanges()
            }
        }
    }

    private func sync(compositionID: UInt64,
                      playheadFrame: Int64,
                      isPlaying: Bool,
                      duration: Int64,
                      frameRate: Double,
                      previewBackdrop: MS_PREVIEWER_BACKDROP,
                      revision _: Int)
    {
        let wasPlaying = self.isPlaying
        self.compositionID = compositionID
        self.isPlaying = isPlaying
        if wasPlaying && !isPlaying {
            publishPlayhead(self.playheadFrame)
        } else if !isPlaying || !wasPlaying {
            self.playheadFrame = playheadFrame
            previewFrame = Double(playheadFrame)
        }
        self.duration = max(duration, 1)
        self.frameRate = frameRate
        self.previewBackdrop = previewBackdrop
        lastSyncTime = CACurrentMediaTime()
        configurePlayback(isPlaying, wasPlaying: wasPlaying)
        if !isPlaying || !wasPlaying {
            requestDraw()
        }
    }

    // MARK: - MTKViewDelegate

    func mtkView(_: MTKView, drawableSizeWillChange size: CGSize) {
        guard size.width > 0, size.height > 0 else { return }
        applyInitialFitIfNeeded()
        requestDraw()
    }

    func draw(in view: MTKView) {
        // First frame is held back until the initial panel-aware fit, so the
        // composition never flashes centered on the full canvas.
        guard didApplyInitialFit else { return }
        let drawStartTime = CACurrentMediaTime()
        let syncToDrawMilliseconds = lastSyncTime.map { (drawStartTime - $0) * 1000 }
        let requestToDrawMilliseconds = lastDrawRequestTime.map { (drawStartTime - $0) * 1000 }
        let drawIntervalMilliseconds = lastDrawStartTime.map { (drawStartTime - $0) * 1000 }
        lastDrawStartTime = drawStartTime
        advancePlayheadForDraw(at: drawStartTime)

        if canvas == nil {
            guard let created = ms_canvas_create(Unmanaged.passUnretained(view).toOpaque()) else {
                return
            }
            canvas = created
            ms_canvas_set_view_transform(created, Float(canvasZoom), Float(canvasPan.x), Float(canvasPan.y))
        }
        guard let canvas else { return }
        updateSelectionOutline()
        ms_canvas_set_preview_backdrop(canvas, previewBackdrop)
        let profile = document.core.drawFrameProfiled(canvas: canvas, compositionID: compositionID, frameTime: previewFrame)
        // Reveal only after the first frame is presented; an empty CAMetalLayer
        // would otherwise flash black.
        metalView.isHidden = false
        logSlowFrame(profile)
        logDrawScheduling(syncToDrawMilliseconds: syncToDrawMilliseconds,
                          requestToDrawMilliseconds: requestToDrawMilliseconds,
                          drawIntervalMilliseconds: drawIntervalMilliseconds,
                          profile: profile)
    }

    // MARK: - Playback

    private func configurePlayback(_ playing: Bool, wasPlaying: Bool) {
        let preferredFramesPerSecond = playing ? displayFramesPerSecond() : max(1, Int(frameRate.rounded()))
        metalView.preferredFramesPerSecond = preferredFramesPerSecond
        playbackFramesPerSecond = Double(preferredFramesPerSecond)
        metalView.enableSetNeedsDisplay = !playing
        metalView.isPaused = !playing
        if playing {
            if !wasPlaying {
                lastPlaybackDrawTime = nil
                lastPlayheadPublishTime = CACurrentMediaTime()
                lastDrawStartTime = nil
                resetProfileStats()
            }
        } else if wasPlaying {
            lastPlaybackDrawTime = nil
        }
    }

    private func displayFramesPerSecond() -> Int {
        let screen = view.window?.screen ?? UIScreen.main
        return max(Int(frameRate.rounded()), screen.maximumFramesPerSecond)
    }

    private func advancePlayheadForDraw(at drawTime: CFTimeInterval) {
        guard isPlaying else {
            currentDrawGapMilliseconds = 0
            currentAdvancedFrames = 0
            currentSkippedFrames = 0
            currentPublishMilliseconds = 0
            return
        }
        let drawGap = lastPlaybackDrawTime.map { drawTime - $0 }
        lastPlaybackDrawTime = drawTime
        currentDrawGapMilliseconds = (drawGap ?? 0) * 1000

        let drawInterval = 1.0 / max(playbackFramesPerSecond, 1)
        let durationFrames = Double(max(duration, 1))
        let oldIntegerFrame = Int64(previewFrame.rounded(.down))
        previewFrame += (drawGap ?? drawInterval) * frameRate
        if previewFrame >= durationFrames {
            previewFrame.formTruncatingRemainder(dividingBy: durationFrames)
        }
        playheadFrame = Int64(previewFrame.rounded(.down))
        if playheadFrame >= oldIntegerFrame {
            currentAdvancedFrames = Int(playheadFrame - oldIntegerFrame)
        } else {
            currentAdvancedFrames = Int(Int64(durationFrames) - oldIntegerFrame + playheadFrame)
        }
        currentSkippedFrames = max(0, currentAdvancedFrames - 1)

        let advanceMilliseconds = publishPlayheadIfNeeded(playheadFrame)
        currentPublishMilliseconds = advanceMilliseconds
        logPlaybackTiming(drawGap: drawGap,
                          displayInterval: drawInterval,
                          advancedFrames: currentAdvancedFrames,
                          advanceMilliseconds: advanceMilliseconds)
    }

    private func publishPlayheadIfNeeded(_ frame: Int64) -> Double {
        let now = CACurrentMediaTime()
        let publishInterval = 1.0 / max(frameRate, 1)
        guard now - lastPlayheadPublishTime >= publishInterval else {
            return 0
        }
        lastPlayheadPublishTime = now
        let publishStartTime = CACurrentMediaTime()
        publishPlayhead(frame)
        return (CACurrentMediaTime() - publishStartTime) * 1000
    }

    private func publishPlayhead(_ frame: Int64) {
        editorState.playheadFrame = frame
    }

    private func requestDraw() {
        lastDrawRequestTime = CACurrentMediaTime()
        metalView.setNeedsDisplay()
    }

    @objc private func handleCanvasTap(_ gesture: UITapGestureRecognizer) {
        guard gesture.state == .ended else { return }
        let viewPoint = gesture.location(in: view)
        let additive = gesture.modifierFlags.contains(.shift) || KeyboardModifiers.shiftPressed
        if let layerID = hitTestLayer(at: viewPoint) {
            selectLayer(layerID, additive: additive)
        } else if !additive {
            clearSelection()
            updateSelectionOutline()
            requestDraw()
        }
    }

    // MARK: - Canvas view transform (zoom & pan)

    /// Applies the panel-aware fit once, after the view appeared with a valid drawable.
    func applyInitialFitIfNeeded() {
        guard !didApplyInitialFit,
              viewDidAppearOnce,
              view.bounds.width > 0,
              view.bounds.height > 0,
              metalView.drawableSize.width > 0,
              metalView.drawableSize.height > 0
        else {
            return
        }
        didApplyInitialFit = true
        resetViewTransform()
    }

    @objc private func handleCanvasDoubleTap() {
        resetViewTransform()
    }

    private func resetViewTransform() {
        let compositionSize = document.core.size(compositionID: compositionID)
        let drawableSize = metalView.drawableSize
        let freeRect = view.bounds
            .inset(by: viewportInsetsProvider?() ?? .zero)
            .insetBy(dx: Self.viewportFitMargin, dy: Self.viewportFitMargin)
        guard compositionSize.width > 0,
              compositionSize.height > 0,
              drawableSize.width > 0,
              drawableSize.height > 0,
              view.bounds.width > 0,
              freeRect.width > 0,
              freeRect.height > 0
        else {
            canvasZoom = 1
            canvasPan = .zero
            pushViewTransform()
            return
        }
        // Base fit mirroring the adapter's on-screen transform, then re-solve
        // zoom/pan so the composition lands fully inside the unobscured rect.
        let fitScale = min(1, min(drawableSize.width / compositionSize.width,
                                  drawableSize.height / compositionSize.height))
        let destWidth = max(1, floor(compositionSize.width * fitScale + 0.000001))
        let destHeight = max(1, floor(compositionSize.height * fitScale + 0.000001))
        let fitScaleX = destWidth / compositionSize.width
        let fitScaleY = destHeight / compositionSize.height
        let offsetX = floor((drawableSize.width - destWidth) * 0.5)
        let offsetY = floor((drawableSize.height - destHeight) * 0.5)
        let contentsScale = drawableSize.width / view.bounds.width

        let targetScale = min(1, min(freeRect.width * contentsScale / compositionSize.width,
                                     freeRect.height * contentsScale / compositionSize.height))
        let zoom = min(targetScale / fitScaleX, targetScale / fitScaleY)
        canvasZoom = min(max(zoom, Self.minCanvasZoom), Self.maxCanvasZoom)
        let displayedWidth = compositionSize.width * canvasZoom * fitScaleX / contentsScale
        let displayedHeight = compositionSize.height * canvasZoom * fitScaleY / contentsScale
        canvasPan = CGPoint(x: freeRect.midX - displayedWidth * 0.5 - canvasZoom * offsetX / contentsScale,
                            y: freeRect.midY - displayedHeight * 0.5 - canvasZoom * offsetY / contentsScale)
        pushViewTransform()
    }

    @objc private func handleCanvasPinch(_ gesture: UIPinchGestureRecognizer) {
        switch gesture.state {
        case .began:
            lastPinchScale = gesture.scale

        case .changed:
            let delta = gesture.scale / lastPinchScale
            lastPinchScale = gesture.scale
            applyZoom(delta: delta, anchor: gesture.location(in: view))

        default:
            break
        }
    }

    @objc private func handleCanvasTouchPan(_ gesture: UIPanGestureRecognizer) {
        switch gesture.state {
        case .began:
            lastTouchPanTranslation = .zero

        case .changed:
            let translation = gesture.translation(in: view)
            applyPan(delta: CGPoint(x: translation.x - lastTouchPanTranslation.x,
                                    y: translation.y - lastTouchPanTranslation.y))
            lastTouchPanTranslation = translation

        default:
            lastTouchPanTranslation = .zero
        }
    }

    @objc private func handleCanvasHover(_ gesture: UIHoverGestureRecognizer) {
        switch gesture.state {
        case .began, .changed:
            lastPointerLocation = gesture.location(in: view)
        default:
            break
        }
    }

    @objc private func handleCanvasScroll(_ gesture: UIPanGestureRecognizer) {
        // A trackpad pinch can emit scroll events at the same time; the pinch
        // handler owns the transform while it is active.
        if let pinchGesture, pinchGesture.state == .began || pinchGesture.state == .changed {
            lastScrollTranslation = gesture.translation(in: view)
            return
        }
        switch gesture.state {
        case .began:
            lastScrollTranslation = .zero

        case .changed:
            let translation = gesture.translation(in: view)
            let delta = CGPoint(x: translation.x - lastScrollTranslation.x,
                                y: translation.y - lastScrollTranslation.y)
            lastScrollTranslation = translation
            if gesture.modifierFlags.contains(.command) {
                // location(in:) is unreliable for scroll-driven recognizers;
                // use the hover-tracked pointer position instead.
                let anchor = lastPointerLocation ?? CGPoint(x: view.bounds.midX, y: view.bounds.midY)
                applyZoom(delta: exp(-delta.y * Self.scrollZoomSensitivity),
                          anchor: anchor)
            } else {
                applyPan(delta: delta)
            }

        default:
            lastScrollTranslation = .zero
        }
    }

    private func applyZoom(delta: CGFloat, anchor: CGPoint) {
        let zoom = min(max(canvasZoom * delta, Self.minCanvasZoom), Self.maxCanvasZoom)
        let applied = zoom / canvasZoom
        // Keeps the scene point under the anchor fixed while zooming.
        canvasPan = CGPoint(x: anchor.x - applied * (anchor.x - canvasPan.x),
                            y: anchor.y - applied * (anchor.y - canvasPan.y))
        canvasZoom = zoom
        pushViewTransform()
    }

    private func applyPan(delta: CGPoint) {
        canvasPan.x += delta.x
        canvasPan.y += delta.y
        pushViewTransform()
    }

    private func pushViewTransform() {
        // The canvas is created lazily in draw(in:) and picks up the current
        // zoom/pan there; the draw must still be scheduled before it exists.
        if let canvas {
            ms_canvas_set_view_transform(canvas, Float(canvasZoom), Float(canvasPan.x), Float(canvasPan.y))
        }
        requestDraw()
    }

    // MARK: - Layer selection & free transform

    @objc private func handleLayerDrag(_ gesture: UIPanGestureRecognizer) {
        guard !isPlaying else { return }
        let shift = gesture.modifierFlags.contains(.shift) || KeyboardModifiers.shiftPressed
        let alternate = gesture.modifierFlags.contains(.alternate) || KeyboardModifiers.alternatePressed
        switch gesture.state {
        case .began:
            beginFreeTransform(at: gesture.location(in: view), shift: shift, alternate: alternate)
        case .changed:
            updateFreeTransform(at: gesture.location(in: view), shift: shift, alternate: alternate)
        case .ended, .cancelled, .failed:
            endFreeTransform()
        default:
            break
        }
    }

    private func beginFreeTransform(at viewPoint: CGPoint, shift: Bool, alternate: Bool) {
        freeTransformDrag = nil
        freeTransformDidMove = false
        guard let scenePoint = scenePoint(fromViewPoint: viewPoint) else {
            return
        }

        let selectedIDs = editorState.selectedLayerIDs
        if !selectedIDs.isEmpty,
           let handles = currentSelectionHandles()
        {
            let hit = hitTestHandle(handles, atViewPoint: viewPoint)
            if hit != .NONE {
                beginHandleTransform(hit: hit,
                                     handles: handles,
                                     scenePoint: scenePoint,
                                     alternate: alternate)
                return
            }
        }

        guard let layerID = hitTestLayer(at: viewPoint),
              !document.core.layerIsLocked(layerID)
        else {
            return
        }
        selectLayer(layerID, additive: shift)
        let layerIDs = editorState.selectedLayerIDs.filter { !document.core.layerIsLocked($0) }
        guard !layerIDs.isEmpty else {
            return
        }
        let starts = FreeTransformDrag.makeLayerStarts(core: document.core, layerIDs: layerIDs, frame: playheadFrame)
        document.core.beginDrag()
        freeTransformDrag = FreeTransformDrag(kind: .move,
                                              layerStarts: starts,
                                              startScenePoint: scenePoint,
                                              startHandles: currentSelectionHandles() ?? SelectionHandlesSnapshot(),
                                              pivotScene: .zero,
                                              localPivotRelative: nil,
                                              editName: layerIDs.count > 1 ? "Move Layers" : "Move Layer")
    }

    private func beginHandleTransform(hit: MS_SELECTION_HANDLE,
                                      handles: SelectionHandlesSnapshot,
                                      scenePoint: CGPoint,
                                      alternate: Bool)
    {
        let kind: FreeTransformKind
        let editName: String
        if hit == .ANCHOR {
            kind = .anchor
            editName = "Move Anchor"
        } else if hit.isRotate {
            kind = .rotate
            editName = editorState.selectedLayerIDs.count > 1 ? "Rotate Layers" : "Rotate Layer"
        } else if let corner = hit.cornerIndex {
            kind = .scaleCorner(corner)
            editName = editorState.selectedLayerIDs.count > 1 ? "Scale Layers" : "Scale Layer"
        } else if let edge = hit.edgeIndex {
            kind = .scaleEdge(edge)
            editName = editorState.selectedLayerIDs.count > 1 ? "Scale Layers" : "Scale Layer"
        } else {
            return
        }

        var layerIDs = editorState.selectedLayerIDs.filter { !document.core.layerIsLocked($0) }
        if hit == .ANCHOR {
            layerIDs = layerIDs.filter { $0 == handles.primaryLayerID }
        }
        guard !layerIDs.isEmpty else {
            return
        }
        let starts = FreeTransformDrag.makeLayerStarts(core: document.core, layerIDs: layerIDs, frame: playheadFrame)
        let pivot = FreeTransformDrag.pivot(for: kind, handles: handles, alternate: alternate)
        let localRel = starts.first.flatMap {
            FreeTransformDrag.localPivotRelative(for: kind, handles: handles, start: $0, alternate: alternate)
        }
        document.core.beginDrag()
        freeTransformDrag = FreeTransformDrag(kind: kind,
                                              layerStarts: starts,
                                              startScenePoint: scenePoint,
                                              startHandles: handles,
                                              pivotScene: pivot,
                                              localPivotRelative: localRel,
                                              editName: editName)
    }

    private func updateFreeTransform(at viewPoint: CGPoint, shift: Bool, alternate: Bool) {
        guard let freeTransformDrag,
              let scenePoint = scenePoint(fromViewPoint: viewPoint)
        else {
            return
        }
        let delta = CGPoint(x: scenePoint.x - freeTransformDrag.startScenePoint.x,
                            y: scenePoint.y - freeTransformDrag.startScenePoint.y)
        guard abs(delta.x) > 0.001 || abs(delta.y) > 0.001 else {
            return
        }
        freeTransformDrag.apply(core: document.core,
                                frame: playheadFrame,
                                scenePoint: scenePoint,
                                shift: shift,
                                alternate: alternate)
        freeTransformDidMove = true
        requestDraw()
    }

    private func endFreeTransform() {
        defer {
            freeTransformDrag = nil
            freeTransformDidMove = false
        }
        guard let freeTransformDrag else {
            return
        }
        document.core.endDrag()
        guard freeTransformDidMove else {
            return
        }
        registerEdit(freeTransformDrag.editName)
    }

    private func selectLayer(_ layerID: UInt64, additive: Bool = false) {
        editorState.selectLayer(layerID, additive: additive)
        updateSelectionOutline()
        requestDraw()
    }

    private func hitTestLayer(at viewPoint: CGPoint) -> UInt64? {
        guard let scenePoint = scenePoint(fromViewPoint: viewPoint) else {
            return nil
        }
        return document.core.hitTestLayer(compositionID: compositionID,
                                          frameTime: previewFrame,
                                          point: scenePoint,
                                          tolerance: hitToleranceSceneUnits())
    }

    private func currentSelectionHandles() -> SelectionHandlesSnapshot? {
        let layerIDs = editorState.selectedLayerIDs
        guard !layerIDs.isEmpty else {
            return nil
        }
        return document.core.selectionHandles(compositionID: compositionID,
                                              frameTime: previewFrame,
                                              layerIDs: layerIDs,
                                              primaryLayerID: layerIDs.last ?? 0)
    }

    /// Hit-tests selection chrome in view points so handle size matches what is drawn.
    private func hitTestHandle(_ handles: SelectionHandlesSnapshot, atViewPoint viewPoint: CGPoint) -> MS_SELECTION_HANDLE {
        guard handles.valid, let transform = currentScreenTransform() else {
            return .NONE
        }
        let radius = Self.handleHitPoints
        func isNear(_ scenePoint: CGPoint) -> Bool {
            let point = transform.viewPoint(fromScenePoint: scenePoint)
            let dx = point.x - viewPoint.x
            let dy = point.y - viewPoint.y
            return dx * dx + dy * dy <= radius * radius
        }
        if isNear(handles.anchor) {
            return .ANCHOR
        }
        let cornerHits: [MS_SELECTION_HANDLE] = [.SCALE_CORNER0, .SCALE_CORNER1, .SCALE_CORNER2, .SCALE_CORNER3]
        for index in 0 ..< 4 where isNear(handles.corners[index]) {
            return cornerHits[index]
        }
        let edgeHits: [MS_SELECTION_HANDLE] = [.SCALE_EDGE0, .SCALE_EDGE1, .SCALE_EDGE2, .SCALE_EDGE3]
        for index in 0 ..< 4 where isNear(handles.edgeMids[index]) {
            return edgeHits[index]
        }

        let centerView = transform.viewPoint(fromScenePoint: handles.center)
        let rotateHits: [MS_SELECTION_HANDLE] = [.ROTATE0, .ROTATE1, .ROTATE2, .ROTATE3]
        for index in 0 ..< 4 {
            let cornerView = transform.viewPoint(fromScenePoint: handles.corners[index])
            let outwardX = cornerView.x - centerView.x
            let outwardY = cornerView.y - centerView.y
            let length = hypot(outwardX, outwardY)
            guard length > 0.001 else {
                continue
            }
            let axisX = outwardX / length
            let axisY = outwardY / length
            let deltaX = viewPoint.x - cornerView.x
            let deltaY = viewPoint.y - cornerView.y
            let along = deltaX * axisX + deltaY * axisY
            if along < Self.rotateInnerPoints || along > Self.rotateOuterPoints {
                continue
            }
            let lateralX = deltaX - axisX * along
            let lateralY = deltaY - axisY * along
            if hypot(lateralX, lateralY) <= radius {
                return rotateHits[index]
            }
        }
        return .NONE
    }

    private func scenePoint(fromViewPoint point: CGPoint) -> CGPoint? {
        guard let transform = currentScreenTransform() else {
            return nil
        }
        return CGPoint(x: (point.x * transform.contentsScale - transform.panX - transform.zoomedOffsetX) / transform.scaleX,
                       y: (point.y * transform.contentsScale - transform.panY - transform.zoomedOffsetY) / transform.scaleY)
    }

    private func updateSelectionOutline() {
        guard let canvas else {
            return
        }
        let selectedLayerIDs = editorState.selectedLayerIDs
        selectedLayerIDs.withUnsafeBufferPointer { buffer in
            ms_canvas_set_selected_layers(canvas, buffer.baseAddress, buffer.count)
        }
    }

    private func hitToleranceSceneUnits() -> CGFloat {
        Self.hitTolerancePoints * viewPointSceneUnits()
    }

    private func viewPointSceneUnits() -> CGFloat {
        guard let transform = currentScreenTransform() else {
            return 1
        }
        return transform.contentsScale / max(min(abs(transform.scaleX), abs(transform.scaleY)), 0.001)
    }

    private func currentScreenTransform() -> CanvasScreenTransform? {
        let compositionSize = document.core.size(compositionID: compositionID)
        let drawableSize = metalView.drawableSize
        guard compositionSize.width > 0,
              compositionSize.height > 0,
              drawableSize.width > 0,
              drawableSize.height > 0,
              view.bounds.width > 0,
              view.bounds.height > 0
        else {
            return nil
        }
        let fitScale = min(1, min(drawableSize.width / compositionSize.width,
                                  drawableSize.height / compositionSize.height))
        let destWidth = max(1, floor(compositionSize.width * fitScale + 0.000001))
        let destHeight = max(1, floor(compositionSize.height * fitScale + 0.000001))
        let fitScaleX = destWidth / compositionSize.width
        let fitScaleY = destHeight / compositionSize.height
        let offsetX = floor((drawableSize.width - destWidth) * 0.5)
        let offsetY = floor((drawableSize.height - destHeight) * 0.5)
        let contentsScale = drawableSize.width / view.bounds.width
        return CanvasScreenTransform(contentsScale: contentsScale,
                                     scaleX: canvasZoom * fitScaleX,
                                     scaleY: canvasZoom * fitScaleY,
                                     panX: canvasPan.x * contentsScale,
                                     panY: canvasPan.y * contentsScale,
                                     zoomedOffsetX: canvasZoom * offsetX,
                                     zoomedOffsetY: canvasZoom * offsetY)
    }

    private struct CanvasScreenTransform {
        let contentsScale: CGFloat
        let scaleX: CGFloat
        let scaleY: CGFloat
        let panX: CGFloat
        let panY: CGFloat
        let zoomedOffsetX: CGFloat
        let zoomedOffsetY: CGFloat

        func viewPoint(fromScenePoint point: CGPoint) -> CGPoint {
            CGPoint(x: (point.x * scaleX + panX + zoomedOffsetX) / contentsScale,
                    y: (point.y * scaleY + panY + zoomedOffsetY) / contentsScale)
        }
    }

    private func logSlowFrame(_ profile: CanvasFrameProfile) {
        #if DEBUG
            guard profile.drewFrame else {
                return
            }
            let frameBudgetMilliseconds = 1000.0 / max(frameRate, 1)
            let now = CACurrentMediaTime()
            if profileStatsStartTime == 0 {
                profileStatsStartTime = now
            }
            profileFrameCount += 1
            profileDroppedFrameCount += currentSkippedFrames
            profileTotalRenderMilliseconds += profile.totalMilliseconds
            profileMaxRenderMilliseconds = max(profileMaxRenderMilliseconds, profile.totalMilliseconds)
            profileMaxDrawGapMilliseconds = max(profileMaxDrawGapMilliseconds, currentDrawGapMilliseconds)

            guard now - profileStatsStartTime >= 0.5 else {
                return
            }
            let elapsed = max(now - profileStatsStartTime, 0.001)
            let observedFramesPerSecond = Double(profileFrameCount) / elapsed
            let averageRenderMilliseconds = profileTotalRenderMilliseconds / Double(max(profileFrameCount, 1))
            print(String(format: "Canvas profile %.2fs: draw fps %.1f, frames %d, skipped %d, gap max %.2f ms, render avg %.2f max %.2f (budget %.2f), last render %.2f, publish %.2f, layers %d, commands %d",
                         elapsed,
                         observedFramesPerSecond,
                         profileFrameCount,
                         profileDroppedFrameCount,
                         profileMaxDrawGapMilliseconds,
                         averageRenderMilliseconds,
                         profileMaxRenderMilliseconds,
                         frameBudgetMilliseconds,
                         profile.totalMilliseconds,
                         currentPublishMilliseconds,
                         profile.layerCount,
                         profile.drawCommandCount))
            resetProfileStats()
        #endif
    }

    private func resetProfileStats() {
        profileStatsStartTime = 0
        profileFrameCount = 0
        profileDroppedFrameCount = 0
        profileTotalRenderMilliseconds = 0
        profileMaxRenderMilliseconds = 0
        profileMaxDrawGapMilliseconds = 0
    }

    private func logDrawScheduling(syncToDrawMilliseconds: Double?,
                                   requestToDrawMilliseconds: Double?,
                                   drawIntervalMilliseconds: Double?,
                                   profile: CanvasFrameProfile)
    {
        #if DEBUG
            let frameBudgetMilliseconds = 1000.0 / max(frameRate, 1)
            let slowRequest = !isPlaying && (requestToDrawMilliseconds ?? 0) > frameBudgetMilliseconds
            let slowInterval = isPlaying && (drawIntervalMilliseconds ?? 0) > frameBudgetMilliseconds * 1.5
            guard slowRequest || slowInterval else {
                return
            }
            let now = CACurrentMediaTime()
            guard now - lastPlaybackTimingLogTime > 0.5 else {
                return
            }
            lastPlaybackTimingLogTime = now
            print(String(format: "Canvas schedule: sync->draw %.2f ms, request->draw %.2f ms, draw interval %.2f ms, render %.2f ms",
                         syncToDrawMilliseconds ?? 0,
                         requestToDrawMilliseconds ?? 0,
                         drawIntervalMilliseconds ?? 0,
                         profile.totalMilliseconds))
        #endif
    }

    private func logPlaybackTiming(drawGap: CFTimeInterval?,
                                   displayInterval: CFTimeInterval,
                                   advancedFrames: Int,
                                   advanceMilliseconds: Double)
    {
        #if DEBUG
            guard let drawGap else {
                return
            }
            let displayIntervalMilliseconds = displayInterval * 1000
            let drawGapMilliseconds = drawGap * 1000
            guard drawGapMilliseconds > displayIntervalMilliseconds * 1.5 ||
                advanceMilliseconds > displayIntervalMilliseconds
            else {
                return
            }
            let now = CACurrentMediaTime()
            guard now - lastPlaybackTimingLogTime > 0.5 else {
                return
            }
            lastPlaybackTimingLogTime = now
            print(String(format: "Canvas playback: draw gap %.2f ms (target %.2f), publish %.2f ms, frames %d",
                         drawGapMilliseconds,
                         displayIntervalMilliseconds,
                         advanceMilliseconds,
                         advancedFrames))
        #endif
    }
}
