//
//  TimelineRulerView.swift
//  MotionStudioApp
//
//  Timeline ruler drawing.
//

import SwiftUI

struct RulerCanvas: View {
    let duration: Int64
    let frameRate: Double
    let pointsPerFrame: CGFloat
    let scrollX: CGFloat
    let contentInset: CGFloat

    var body: some View {
        Canvas { context, size in
            let total = Int(duration)
            let second = max(Int(frameRate.rounded()), 1)
            let majorStep = rulerMajorStep(pointsPerFrame: pointsPerFrame)
            let minorStep = max(majorStep / 5, 1)
            let visibleTrackWidth = max(0, size.width - contentInset * 2)
            let firstFrame = max(0, Int(floor(scrollX / pointsPerFrame)))
            let lastFrame = min(total, Int(ceil((scrollX + visibleTrackWidth) / pointsPerFrame)) + minorStep)
            let firstTick = firstFrame + (minorStep - firstFrame % minorStep) % minorStep
            for frame in stride(from: firstTick, through: lastFrame, by: minorStep) {
                let x = contentInset + timelineX(for: Int64(frame), pointsPerFrame: pointsPerFrame) - scrollX
                guard x >= contentInset, x <= size.width - contentInset else { continue }
                let isMajor = frame % majorStep == 0
                var tick = Path()
                tick.move(to: CGPoint(x: x, y: isMajor ? 8 : 15))
                tick.addLine(to: CGPoint(x: x, y: 22))
                context.stroke(tick, with: .color(.secondary), lineWidth: isMajor ? 1 : 0.5)
                if isMajor {
                    let label = context.resolve(Text(rulerLabel(frame: frame, second: second)).font(.system(size: 9)))
                    let labelWidth = label.measure(in: size).width
                    if x + 8 + labelWidth > size.width - contentInset {
                        context.draw(label, at: CGPoint(x: x - 4, y: 8), anchor: .trailing)
                    } else {
                        context.draw(label, at: CGPoint(x: x + 8, y: 8), anchor: .leading)
                    }
                }
            }
        }
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
