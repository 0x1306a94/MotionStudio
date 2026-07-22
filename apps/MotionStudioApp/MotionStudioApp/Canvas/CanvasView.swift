//
//  CanvasView.swift
//  MotionStudioApp
//
//  Live preview canvas: MTKView rendered directly by the tgfx on-screen
//  adapter via the bridge. Playback is driven by MTKView's draw loop.
//

import MetalKit
import SwiftUI
import UIKit

struct CanvasView: UIViewRepresentable {
    let core: MotionDocumentCore
    let compositionID: UInt64
    let playheadFrame: Int64
    let isPlaying: Bool
    let duration: Int64
    let frameRate: Double
    let previewBackdrop: PreviewBackdrop
    /// Document revision; read so any model mutation (not just playhead moves)
    /// re-evaluates this view and triggers updateUIView to redraw the canvas.
    let revision: Int
    let onAdvancePlayhead: @MainActor (Int64) -> Void

    func makeCoordinator() -> Coordinator {
        Coordinator(core: core,
                    compositionID: compositionID,
                    duration: duration,
                    frameRate: frameRate,
                    onAdvancePlayhead: onAdvancePlayhead)
    }

    func makeUIView(context: Context) -> MTKView {
        context.coordinator.makeCanvasView()
    }

    func updateUIView(_ view: MTKView, context: Context) {
        context.coordinator.sync(playheadFrame: playheadFrame,
                                 isPlaying: isPlaying,
                                 duration: duration,
                                 frameRate: frameRate,
                                 previewBackdrop: previewBackdrop,
                                 view: view)
    }

    /// Drives the MTKView and the bridge canvas. MTKView delegate callbacks
    /// arrive on the main thread.
    @MainActor
    final class Coordinator: NSObject, MTKViewDelegate {
        private let core: MotionDocumentCore
        private let compositionID: UInt64
        private let onAdvancePlayhead: @MainActor (Int64) -> Void

        private nonisolated(unsafe) var canvas: OpaquePointer?
        private weak var canvasView: MTKView?
        private var playheadFrame: Int64 = 0
        private var previewFrame: Double = 0
        private var duration: Int64 = 0
        private var previewBackdrop: PreviewBackdrop = .transparent
        private var frameRate: Double
        private var playbackFramesPerSecond: Double
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

        init(core: MotionDocumentCore,
             compositionID: UInt64,
             duration: Int64,
             frameRate: Double,
             onAdvancePlayhead: @escaping @MainActor (Int64) -> Void)
        {
            self.core = core
            self.compositionID = compositionID
            self.duration = max(duration, 1)
            self.frameRate = frameRate
            playbackFramesPerSecond = frameRate
            self.onAdvancePlayhead = onAdvancePlayhead
            super.init()
        }

        deinit {
            if let canvas {
                ms_canvas_destroy(canvas)
            }
        }

        func makeCanvasView() -> MTKView {
            let view = MTKView()
            view.device = MTLCreateSystemDefaultDevice()
            view.isPaused = true
            view.enableSetNeedsDisplay = true
            view.framebufferOnly = true
            view.preferredFramesPerSecond = max(1, Int(frameRate.rounded()))
            view.delegate = self
            return view
        }

        func sync(playheadFrame: Int64,
                  isPlaying: Bool,
                  duration: Int64,
                  frameRate: Double,
                  previewBackdrop: PreviewBackdrop,
                  view: MTKView)
        {
            let wasPlaying = self.isPlaying
            self.isPlaying = isPlaying
            if wasPlaying && !isPlaying {
                onAdvancePlayhead(self.playheadFrame)
            } else if !isPlaying || !wasPlaying {
                self.playheadFrame = playheadFrame
                previewFrame = Double(playheadFrame)
            }
            self.duration = max(duration, 1)
            self.frameRate = frameRate
            self.previewBackdrop = previewBackdrop
            canvasView = view
            lastSyncTime = CACurrentMediaTime()
            configurePlayback(isPlaying, wasPlaying: wasPlaying, view: view)
            if !isPlaying || !wasPlaying {
                requestDraw(view: view)
            }
        }

        // MARK: - MTKViewDelegate

        func mtkView(_: MTKView, drawableSizeWillChange _: CGSize) {}

        func draw(in view: MTKView) {
            let drawStartTime = CACurrentMediaTime()
            let syncToDrawMilliseconds = lastSyncTime.map { (drawStartTime - $0) * 1000 }
            let requestToDrawMilliseconds = lastDrawRequestTime.map { (drawStartTime - $0) * 1000 }
            let drawIntervalMilliseconds = lastDrawStartTime.map { (drawStartTime - $0) * 1000 }
            lastDrawStartTime = drawStartTime
            advancePlayheadForDraw(at: drawStartTime)

            if canvas == nil {
                guard let created = ms_canvas_create(Unmanaged.passUnretained(view).toOpaque())
                else {
                    return
                }
                canvas = created
            }
            guard let canvas else {
                return
            }
            ms_canvas_set_preview_backdrop(canvas, previewBackdrop.rawValue)
            let profile = core.drawFrameProfiled(canvas: canvas,
                                                 compositionID: compositionID,
                                                 frameTime: previewFrame)
            logSlowFrame(profile)
            logDrawScheduling(syncToDrawMilliseconds: syncToDrawMilliseconds,
                              requestToDrawMilliseconds: requestToDrawMilliseconds,
                              drawIntervalMilliseconds: drawIntervalMilliseconds,
                              profile: profile)
        }

        // MARK: - Playback

        private func configurePlayback(_ playing: Bool, wasPlaying: Bool, view: MTKView) {
            let preferredFramesPerSecond = playing ? displayFramesPerSecond(for: view) : max(1, Int(frameRate.rounded()))
            view.preferredFramesPerSecond = preferredFramesPerSecond
            playbackFramesPerSecond = Double(preferredFramesPerSecond)
            view.enableSetNeedsDisplay = !playing
            view.isPaused = !playing
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

        private func displayFramesPerSecond(for view: MTKView) -> Int {
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
            onAdvancePlayhead(frame)
            return (CACurrentMediaTime() - publishStartTime) * 1000
        }

        private func requestDraw(view: MTKView) {
            lastDrawRequestTime = CACurrentMediaTime()
            view.setNeedsDisplay()
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
}
