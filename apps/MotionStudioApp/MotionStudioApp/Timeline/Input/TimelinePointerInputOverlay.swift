//
//  TimelinePointerInputOverlay.swift
//  MotionStudioApp
//
//  Trackpad scroll / hover for the timeline track column.
//  Pinch lives on the track column (ancestor) so iPad finger touches that pass
//  through this overlay still reach the pinch recognizer.
//

import UIKit

@MainActor
final class TimelinePointerInputOverlay: UIView {
    var onPlayheadHoveringChanged: ((Bool) -> Void)?
    /// Trackpad / mouse wheel vertical delta in points (positive = fingers/content move down).
    var onVerticalScroll: ((CGFloat) -> Void)?
    /// Fired when a vertical trackpad/wheel gesture ends so the tracks can spring back.
    var onVerticalScrollEnded: (() -> Void)?

    private let editorState: EditorState
    private var duration: Int64 = 0
    private var pointsPerFrame: CGFloat = pixelsPerFrame
    private var trackWidth: CGFloat = 1
    private var viewportWidth: CGFloat = 1
    private var visiblePlayheadX: CGFloat = 0
    private var contentInset: CGFloat = trackLeadingInset

    private var lastPinchScale: CGFloat = 1
    private var lastScrollTranslation: CGPoint = .zero
    private var lastPointerX: CGFloat?
    private let pinchGesture: UIPinchGestureRecognizer

    init(editorState: EditorState) {
        self.editorState = editorState
        let pinch = UIPinchGestureRecognizer()
        pinchGesture = pinch
        super.init(frame: .zero)
        translatesAutoresizingMaskIntoConstraints = false
        backgroundColor = .clear
        isUserInteractionEnabled = true

        pinch.addTarget(self, action: #selector(handlePinch(_:)))
        pinch.cancelsTouchesInView = false
        pinch.delegate = self

        let hover = UIHoverGestureRecognizer(target: self, action: #selector(handleHover(_:)))
        hover.cancelsTouchesInView = false
        addGestureRecognizer(hover)

        let scroll = UIPanGestureRecognizer(target: self, action: #selector(handleScroll(_:)))
        scroll.maximumNumberOfTouches = 0
        scroll.allowedScrollTypesMask = [.continuous, .discrete]
        scroll.cancelsTouchesInView = false
        scroll.delegate = self
        addGestureRecognizer(scroll)
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    /// Installs pinch on `host` (the track column). Finger hit-testing passes through this
    /// overlay to tracks; an ancestor recognizer still sees those touches.
    func attachPinch(to host: UIView) {
        pinchGesture.view?.removeGestureRecognizer(pinchGesture)
        host.addGestureRecognizer(pinchGesture)
    }

    override func point(inside point: CGPoint, with event: UIEvent?) -> Bool {
        guard let event else {
            return super.point(inside: point, with: event)
        }
        // All finger touches pass through so keyframe / scroll UI keeps receiving them.
        // Trackpad and mouse events stay on the overlay.
        if event.type == .touches {
            return false
        }
        return super.point(inside: point, with: event)
    }

    func update(duration: Int64, pointsPerFrame: CGFloat, trackWidth: CGFloat, viewportWidth: CGFloat,
                visiblePlayheadX: CGFloat, contentInset: CGFloat = trackLeadingInset)
    {
        self.duration = duration
        self.pointsPerFrame = pointsPerFrame
        self.trackWidth = trackWidth
        self.viewportWidth = viewportWidth
        self.visiblePlayheadX = visiblePlayheadX
        self.contentInset = contentInset
    }

    @objc private func handleHover(_ gesture: UIHoverGestureRecognizer) {
        switch gesture.state {
        case .began, .changed:
            let pointerX = gesture.location(in: self).x
            lastPointerX = pointerX
            onPlayheadHoveringChanged?(abs(pointerX - visiblePlayheadX) <= 10)
        default:
            onPlayheadHoveringChanged?(false)
        }
    }

    @objc private func handlePinch(_ gesture: UIPinchGestureRecognizer) {
        switch gesture.state {
        case .began:
            lastPinchScale = gesture.scale
        case .changed:
            let delta = gesture.scale / lastPinchScale
            lastPinchScale = gesture.scale
            applyZoom(delta: delta, anchorX: gesture.location(in: self).x)
        default:
            lastPinchScale = 1
        }
    }

    @objc private func handleScroll(_ gesture: UIPanGestureRecognizer) {
        if pinchGesture.state == .began || pinchGesture.state == .changed {
            lastScrollTranslation = gesture.translation(in: self)
            return
        }
        switch gesture.state {
        case .began:
            lastScrollTranslation = .zero
        case .changed:
            let translation = gesture.translation(in: self)
            let delta = CGPoint(x: translation.x - lastScrollTranslation.x,
                                y: translation.y - lastScrollTranslation.y)
            lastScrollTranslation = translation
            if gesture.modifierFlags.contains(.command) {
                let fallbackAnchor = bounds.width * 0.5
                applyZoom(delta: exp(-delta.y * 0.01), anchorX: lastPointerX ?? fallbackAnchor)
            } else if shouldForwardVerticalScroll(delta: delta, modifierFlags: gesture.modifierFlags) {
                // Overlay sits above tracks; forward vertical wheel/trackpad to the row scroller.
                onVerticalScroll?(delta.y)
            } else {
                applyScroll(delta: timelineScrollDelta(delta: delta, modifierFlags: gesture.modifierFlags))
            }
        case .ended, .cancelled, .failed:
            lastScrollTranslation = .zero
            onVerticalScrollEnded?()
        default:
            lastScrollTranslation = .zero
        }
    }

    private func shouldForwardVerticalScroll(delta: CGPoint, modifierFlags: UIKeyModifierFlags) -> Bool {
        if modifierFlags.contains(.shift) {
            return false
        }
        return abs(delta.y) > abs(delta.x)
    }

    private func timelineScrollDelta(delta: CGPoint, modifierFlags: UIKeyModifierFlags) -> CGFloat {
        if abs(delta.x) >= abs(delta.y) {
            return delta.x
        }
        if modifierFlags.contains(.shift) {
            return delta.y
        }
        return 0
    }

    private func applyScroll(delta: CGFloat) {
        guard delta != 0 else {
            return
        }
        let next = CGFloat(editorState.timelineScrollX) - delta
        editorState.timelineScrollX = Double(clampedScrollX(next, trackWidth: trackWidth))
    }

    private func applyZoom(delta: CGFloat, anchorX: CGFloat) {
        guard delta.isFinite, delta > 0, pointsPerFrame > 0 else {
            return
        }
        let nextPointsPerFrame = min(max(pointsPerFrame * delta, minTimelinePointsPerFrame),
                                     maxTimelinePointsPerFrame)
        let anchorTimelineX = min(max(anchorX - contentInset, 0), viewportWidth)
        let frameUnderAnchor = (CGFloat(editorState.timelineScrollX) + anchorTimelineX) / pointsPerFrame
        let nextTrackWidth = max(CGFloat(timelineTrackFrameSpan(duration)) * nextPointsPerFrame, viewportWidth)
        let nextScrollX = frameUnderAnchor * nextPointsPerFrame - anchorTimelineX
        editorState.timelinePointsPerFrame = Double(nextPointsPerFrame)
        editorState.timelineScrollX = Double(clampedScrollX(nextScrollX, trackWidth: nextTrackWidth))
    }

    private func clampedScrollX(_ value: CGFloat, trackWidth: CGFloat) -> CGFloat {
        min(max(value, 0), max(0, trackWidth - viewportWidth))
    }
}

extension TimelinePointerInputOverlay: UIGestureRecognizerDelegate {
    func gestureRecognizer(_: UIGestureRecognizer,
                           shouldRecognizeSimultaneouslyWith _: UIGestureRecognizer) -> Bool
    {
        true
    }
}
