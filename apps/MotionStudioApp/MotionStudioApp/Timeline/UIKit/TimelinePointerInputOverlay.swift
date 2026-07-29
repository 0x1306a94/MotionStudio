//
//  TimelinePointerInputOverlay.swift
//  MotionStudioApp
//
//  Trackpad pinch / scroll / hover for the UIKit timeline track column.
//

import UIKit

@MainActor
final class TimelinePointerInputOverlay: UIView {
    var onPlayheadHoveringChanged: ((Bool) -> Void)?

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
    private weak var pinchGesture: UIPinchGestureRecognizer?

    init(editorState: EditorState) {
        self.editorState = editorState
        super.init(frame: .zero)
        translatesAutoresizingMaskIntoConstraints = false
        backgroundColor = .clear
        isUserInteractionEnabled = true

        let hover = UIHoverGestureRecognizer(target: self, action: #selector(handleHover(_:)))
        hover.cancelsTouchesInView = false
        addGestureRecognizer(hover)

        let pinch = UIPinchGestureRecognizer(target: self, action: #selector(handlePinch(_:)))
        pinch.cancelsTouchesInView = false
        pinch.delegate = self
        pinchGesture = pinch
        addGestureRecognizer(pinch)

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

    override func point(inside point: CGPoint, with event: UIEvent?) -> Bool {
        guard let event else {
            return super.point(inside: point, with: event)
        }
        if event.type == .touches, (event.allTouches?.count ?? 1) <= 1 {
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
        if let pinchGesture, pinchGesture.state == .began || pinchGesture.state == .changed {
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
            } else {
                applyScroll(delta: timelineScrollDelta(delta: delta, modifierFlags: gesture.modifierFlags))
            }
        default:
            lastScrollTranslation = .zero
        }
    }

    private func timelineScrollDelta(delta: CGPoint, modifierFlags: UIKeyModifierFlags) -> CGFloat {
        if delta.x != 0 {
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
        let nextTrackWidth = max(CGFloat(duration) * nextPointsPerFrame, viewportWidth)
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
