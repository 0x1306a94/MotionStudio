//
//  TimelinePlayheadView.swift
//  MotionStudioApp
//
//  Playhead line overlay; position updates without rebuilding timeline rows.
//

import UIKit

@MainActor
final class TimelinePlayheadView: UIView {
    var onScrub: ((CGFloat) -> Void)?

    private let lineLayer = CALayer()
    private let markerLayer = CAShapeLayer()
    private var contentX: CGFloat?
    private var isHovering = false
    private let markerWidth: CGFloat = 10
    private let markerHeight: CGFloat = 7
    private let hitSlop: CGFloat = 10

    override init(frame: CGRect) {
        super.init(frame: frame)
        translatesAutoresizingMaskIntoConstraints = false
        backgroundColor = .clear
        isOpaque = false
        isUserInteractionEnabled = true

        lineLayer.contentsScale = UIScreen.main.scale
        layer.addSublayer(lineLayer)

        markerLayer.contentsScale = UIScreen.main.scale
        markerLayer.fillColor = UIColor.systemBlue.cgColor
        layer.addSublayer(markerLayer)

        let pan = UIPanGestureRecognizer(target: self, action: #selector(handleScrub(_:)))
        pan.maximumNumberOfTouches = 1
        addGestureRecognizer(pan)
        updateColors()
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        layoutPlayhead()
    }

    override func hitTest(_ point: CGPoint, with event: UIEvent?) -> UIView? {
        guard let contentX, abs(point.x - contentX) <= hitSlop else {
            return nil
        }
        return super.hitTest(point, with: event)
    }

    func setHovering(_ hovering: Bool) {
        guard isHovering != hovering else {
            return
        }
        isHovering = hovering
        updateColors()
    }

    /// Updates playhead x in the track column's coordinate space. Pass nil when off-screen.
    func setContentX(_ x: CGFloat?) {
        contentX = x
        isHidden = x == nil
        layoutPlayhead()
    }

    private func layoutPlayhead() {
        guard let contentX else {
            return
        }
        let color = isHovering ? UIColor.systemRed : UIColor.systemBlue
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        lineLayer.backgroundColor = color.cgColor
        lineLayer.frame = CGRect(x: contentX - 0.5, y: 0, width: 1, height: bounds.height)
        markerLayer.fillColor = color.cgColor
        let markerRect = CGRect(x: contentX - markerWidth / 2, y: 0, width: markerWidth, height: markerHeight)
        markerLayer.frame = markerRect
        markerLayer.path = trianglePath(in: CGRect(origin: .zero, size: markerRect.size)).cgPath
        CATransaction.commit()
    }

    private func updateColors() {
        layoutPlayhead()
    }

    private func trianglePath(in rect: CGRect) -> UIBezierPath {
        let path = UIBezierPath()
        path.move(to: CGPoint(x: rect.minX, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.maxX, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.midX, y: rect.maxY))
        path.close()
        return path
    }

    @objc private func handleScrub(_ recognizer: UIPanGestureRecognizer) {
        switch recognizer.state {
        case .began, .changed:
            onScrub?(recognizer.location(in: self).x)
        default:
            break
        }
    }
}
