//
//  CanvasViewController.swift
//  MotionStudioApp
//
//  UIKit live preview canvas. Owns the MTKView directly and keeps SwiftUI out
//  of the UIKit editor shell's canvas path.
//

import MetalKit
import Observation
import UIKit

@MainActor
final class CanvasViewController: UIViewController, MTKViewDelegate {
    private let document: MotionDocument
    private let editorState: EditorState
    private let clearSelection: () -> Void

    private var metalView: MTKView {
        view as! MTKView
    }

    private nonisolated(unsafe) var canvas: OpaquePointer?
    private var compositionID: UInt64 = 0
    private var playheadFrame: Int64 = 0
    private var previewFrame: Double = 0
    private var duration: Int64 = 1
    private var previewBackdrop: PreviewBackdrop = .transparent
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

    init(document: MotionDocument, editorState: EditorState, clearSelection: @escaping () -> Void) {
        self.document = document
        self.editorState = editorState
        self.clearSelection = clearSelection
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        nil
    }

    deinit {
        if let canvas {
            ms_canvas_destroy(canvas)
        }
    }

    override func loadView() {
        let view = MTKView()
        view.device = MTLCreateSystemDefaultDevice()
        view.isPaused = true
        view.enableSetNeedsDisplay = true
        view.framebufferOnly = true
        view.autoResizeDrawable = true
        view.delegate = self
        self.view = view
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .clear
        let tapGesture = UITapGestureRecognizer(target: self, action: #selector(handleCanvasTap))
        view.addGestureRecognizer(tapGesture)
        syncFromState()
        observeStateChanges()
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
                      previewBackdrop: PreviewBackdrop,
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

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        guard size.width > 0, size.height > 0 else { return }
        requestDraw()
    }

    func draw(in view: MTKView) {
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
        }
        guard let canvas else { return }
        ms_canvas_set_preview_backdrop(canvas, previewBackdrop.rawValue)
        let profile = document.core.drawFrameProfiled(canvas: canvas,
                                                      compositionID: compositionID,
                                                      frameTime: previewFrame)
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

    @objc private func handleCanvasTap() {
        clearSelection()
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
