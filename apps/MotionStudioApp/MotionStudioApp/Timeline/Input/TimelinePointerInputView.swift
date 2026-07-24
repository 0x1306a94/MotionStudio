//
//  TimelinePointerInputView.swift
//  MotionStudioApp
//
//  UIKit pointer, trackpad scroll, and trackpad pinch bridge for the timeline.
//

import SwiftUI
import UIKit

struct TimelinePointerInputView: UIViewRepresentable {
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
