//
//  TimelineRulerCanvasView.swift
//  MotionStudioApp
//
//  Timeline ruler drawing and scrub gesture.
//

import UIKit

@MainActor
final class TimelineRulerCanvasView: UIView {
    var onScrub: ((CGFloat) -> Void)?

    private var duration: Int64 = 0
    private var frameRate: Double = 30
    private var pointsPerFrame: CGFloat = pixelsPerFrame
    private var scrollX: CGFloat = 0
    private var contentInset: CGFloat = trackLeadingInset

    override init(frame: CGRect) {
        super.init(frame: frame)
        translatesAutoresizingMaskIntoConstraints = false
        backgroundColor = .clear
        isOpaque = false
        contentMode = .redraw
        let pan = UIPanGestureRecognizer(target: self, action: #selector(handleScrub(_:)))
        pan.maximumNumberOfTouches = 1
        addGestureRecognizer(pan)
        let tap = UITapGestureRecognizer(target: self, action: #selector(handleTap(_:)))
        addGestureRecognizer(tap)
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    func update(duration: Int64, frameRate: Double, pointsPerFrame: CGFloat, scrollX: CGFloat,
                contentInset: CGFloat = trackLeadingInset)
    {
        self.duration = duration
        self.frameRate = frameRate
        self.pointsPerFrame = pointsPerFrame
        self.scrollX = scrollX
        self.contentInset = contentInset
        setNeedsDisplay()
    }

    override func draw(_: CGRect) {
        guard let context = UIGraphicsGetCurrentContext(), pointsPerFrame > 0 else {
            return
        }
        let total = Int(duration)
        let second = max(Int(frameRate.rounded()), 1)
        let majorStep = rulerMajorStep(pointsPerFrame: pointsPerFrame)
        let minorStep = max(majorStep / 5, 1)
        let visibleTrackWidth = max(0, bounds.width - contentInset * 2)
        let firstFrame = max(0, Int(floor(scrollX / pointsPerFrame)))
        let lastFrame = min(total, Int(ceil((scrollX + visibleTrackWidth) / pointsPerFrame)) + minorStep)
        let firstTick = firstFrame + (minorStep - firstFrame % minorStep) % minorStep
        let secondary = UIColor.secondaryLabel
        let font = UIFont.systemFont(ofSize: 9)
        for frame in stride(from: firstTick, through: lastFrame, by: minorStep) {
            let x = contentInset + timelineX(for: Int64(frame), pointsPerFrame: pointsPerFrame) - scrollX
            guard x >= contentInset, x <= bounds.width - contentInset else {
                continue
            }
            let isMajor = frame % majorStep == 0
            context.setStrokeColor(secondary.cgColor)
            context.setLineWidth(isMajor ? 1 : 0.5)
            context.move(to: CGPoint(x: x, y: isMajor ? 8 : 15))
            context.addLine(to: CGPoint(x: x, y: 22))
            context.strokePath()
            if isMajor {
                let label = rulerLabel(frame: frame, second: second) as NSString
                let attributes: [NSAttributedString.Key: Any] = [
                    .font: font,
                    .foregroundColor: secondary,
                ]
                let labelWidth = label.size(withAttributes: attributes).width
                let labelY: CGFloat = 2
                if x + 8 + labelWidth > bounds.width - contentInset {
                    label.draw(at: CGPoint(x: x - 4 - labelWidth, y: labelY), withAttributes: attributes)
                } else {
                    label.draw(at: CGPoint(x: x + 8, y: labelY), withAttributes: attributes)
                }
            }
        }
    }

    @objc private func handleScrub(_ recognizer: UIPanGestureRecognizer) {
        switch recognizer.state {
        case .began, .changed:
            onScrub?(recognizer.location(in: self).x)
        default:
            break
        }
    }

    @objc private func handleTap(_ recognizer: UITapGestureRecognizer) {
        onScrub?(recognizer.location(in: self).x)
    }

    private func rulerMajorStep(pointsPerFrame: CGFloat) -> Int {
        let targetFrames = max(1, Int((80 / max(pointsPerFrame, 1)).rounded()))
        var scale = 1
        while scale * 10 < targetFrames {
            scale *= 10
        }
        for multiplier in [1, 2, 5, 10] {
            let step = multiplier * scale
            if step >= targetFrames {
                return step
            }
        }
        return 10 * scale
    }

    private func rulerLabel(frame: Int, second: Int) -> String {
        if frame % second == 0 {
            return "\(frame / second)s"
        }
        return "\(frame)f"
    }
}
