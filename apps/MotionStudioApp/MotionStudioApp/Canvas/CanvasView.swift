//
//  CanvasView.swift
//  MotionStudioApp
//
//  Live preview canvas: MTKView rendered directly by the tgfx on-screen
//  adapter via the bridge. Playback is driven by CADisplayLink.
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
                                 view: view)
    }

    /// Drives the MTKView and the bridge canvas. MTKView delegate callbacks
    /// arrive on the main thread.
    @MainActor
    final class Coordinator: NSObject, MTKViewDelegate {
        private let core: MotionDocumentCore
        private let compositionID: UInt64
        private let onAdvancePlayhead: @MainActor (Int64) -> Void

        private var canvas: OpaquePointer?
        private var displayLink: CADisplayLink?
        private weak var canvasView: MTKView?
        private var playheadFrame: Int64 = 0
        private var duration: Int64 = 0
        private var frameRate: Double
        private var carrySeconds: Double = 0

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
            self.onAdvancePlayhead = onAdvancePlayhead
            super.init()
        }

        deinit {
            if let canvas {
                ms_canvas_destroy(canvas)
            }
            displayLink?.invalidate()
        }

        func makeCanvasView() -> MTKView {
            let view = MTKView()
            view.device = MTLCreateSystemDefaultDevice()
            view.isPaused = true
            view.enableSetNeedsDisplay = true
            view.framebufferOnly = true
            view.delegate = self
            return view
        }

        func sync(playheadFrame: Int64, isPlaying: Bool, duration: Int64, view: MTKView) {
            self.playheadFrame = playheadFrame
            self.duration = max(duration, 1)
            canvasView = view
            setPlaying(isPlaying)
            requestDraw(view: view)
        }

        // MARK: - MTKViewDelegate

        func mtkView(_: MTKView, drawableSizeWillChange _: CGSize) {}

        func draw(in view: MTKView) {
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
            core.drawFrame(canvas: canvas, compositionID: compositionID, frame: playheadFrame)
        }

        // MARK: - Playback

        private func setPlaying(_ playing: Bool) {
            let isRunning = displayLink != nil
            guard playing != isRunning else {
                return
            }
            if playing {
                carrySeconds = 0
                let link = CADisplayLink(target: self, selector: #selector(tick(_:)))
                link.add(to: .main, forMode: .common)
                displayLink = link
            } else {
                displayLink?.invalidate()
                displayLink = nil
            }
        }

        @objc
        private func tick(_ link: CADisplayLink) {
            let elapsed = link.targetTimestamp - link.timestamp
            carrySeconds += max(elapsed, 0)
            let frames = Int(carrySeconds * frameRate)
            guard frames > 0 else {
                return
            }
            carrySeconds -= Double(frames) / frameRate
            let next = (playheadFrame + Int64(frames)) % max(duration, 1)
            onAdvancePlayhead(next)
        }

        private func requestDraw(view: MTKView) {
            view.setNeedsDisplay()
        }
    }
}
