//
//  MotionPathInspector.swift
//  MotionStudioApp
//
//  Inspector curve editor for transform.position spatial tangents.
//

import SwiftUI

struct MotionPathInspector: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    @Environment(PlayheadClock.self) private var clock
    let selectedKeyframeIndex: Int?

    private var playheadFrame: Int64 {
        clock.frame
    }

    let isEditable: Bool
    let perform: (String, () -> Void) -> Void
    let onSelectKeyframe: (Int) -> Void

    @State private var isDragging = false

    private let path = "transform.position"

    var body: some View {
        let _ = core.revision
        let frames = core.keyframeFrames(entityID: layerID, path: path)
        if frames.count >= 2 {
            let segment = resolveSegment(frameCount: frames.count)
            let startIndex = segment.start
            let endIndex = segment.end
            let p0 = core.keyframeVec2(entityID: layerID, path: path, index: startIndex)
            let p3 = core.keyframeVec2(entityID: layerID, path: path, index: endIndex)
            let startSpatial = core.keyframeSpatial(entityID: layerID, path: path, index: startIndex)
            let endSpatial = core.keyframeSpatial(entityID: layerID, path: path, index: endIndex)
            let delta = CGVector(dx: p3.dx - p0.dx, dy: p3.dy - p0.dy)
            let defaultOut = CGVector(dx: delta.dx / 3, dy: delta.dy / 3)
            let defaultIn = CGVector(dx: -delta.dx / 3, dy: -delta.dy / 3)
            let outTangent = startSpatial.hasOut ? startSpatial.outTangent : defaultOut
            let inTangent = endSpatial.hasIn ? endSpatial.inTangent : defaultIn
            let c1 = CGPoint(x: p0.dx + outTangent.dx, y: p0.dy + outTangent.dy)
            let c2 = CGPoint(x: p3.dx + inTangent.dx, y: p3.dy + inTangent.dy)

            VStack(alignment: .leading, spacing: 8) {
                Text("Motion Path")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                Text("Segment \(startIndex + 1) → \(endIndex + 1)  ·  f\(frames[startIndex])–f\(frames[endIndex])")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                CubicBezierPad(
                    p0: CGPoint(x: p0.dx, y: p0.dy),
                    p3: CGPoint(x: p3.dx, y: p3.dy),
                    c1: c1,
                    c2: c2,
                    isEditable: isEditable,
                    onChange: { newC1, newC2 in
                        writeSegment(startIndex: startIndex, endIndex: endIndex,
                                     p0: p0, p3: p3, c1: newC1, c2: newC2,
                                     startSpatial: startSpatial, endSpatial: endSpatial)
                    },
                    onDragEnded: {
                        endDragIfNeeded()
                    },
                )
                HStack {
                    Button("Prev KF") {
                        onSelectKeyframe(max(0, startIndex - 1))
                    }
                    .disabled(startIndex <= 0)
                    Spacer()
                    Button("Next KF") {
                        onSelectKeyframe(min(frames.count - 1, endIndex))
                    }
                    .disabled(endIndex >= frames.count - 1)
                }
                .font(.caption)
                .disabled(!isEditable)
            }
            .onAppear {
                if selectedKeyframeIndex == nil {
                    onSelectKeyframe(segment.anchor)
                }
            }
        }
    }

    private struct Segment {
        let start: Int
        let end: Int
        let anchor: Int
    }

    private func resolveSegment(frameCount: Int) -> Segment {
        let anchor: Int
        if let selectedKeyframeIndex, selectedKeyframeIndex >= 0, selectedKeyframeIndex < frameCount {
            anchor = selectedKeyframeIndex
        } else {
            let frames = core.keyframeFrames(entityID: layerID, path: path)
            var best = 0
            var bestDistance = abs(frames[0] - playheadFrame)
            for index in 1 ..< frames.count {
                let distance = abs(frames[index] - playheadFrame)
                if distance < bestDistance {
                    bestDistance = distance
                    best = index
                }
            }
            anchor = best
        }
        if anchor >= frameCount - 1 {
            return Segment(start: frameCount - 2, end: frameCount - 1, anchor: anchor)
        }
        return Segment(start: anchor, end: anchor + 1, anchor: anchor)
    }

    private func writeSegment(startIndex: Int, endIndex: Int, p0: CGVector, p3: CGVector,
                              c1: CGPoint, c2: CGPoint,
                              startSpatial: SpatialTangentsInfo,
                              endSpatial: SpatialTangentsInfo)
    {
        guard isEditable else { return }
        if !isDragging {
            core.beginDrag()
            isDragging = true
        }
        let outTangent = CGVector(dx: c1.x - p0.dx, dy: c1.y - p0.dy)
        let inTangent = CGVector(dx: c2.x - p3.dx, dy: c2.y - p3.dy)
        let startFrame = core.keyframeFrames(entityID: layerID, path: path)[startIndex]
        let endFrame = core.keyframeFrames(entityID: layerID, path: path)[endIndex]
        core.setSpatialTangents(entityID: layerID, path: path, frame: startFrame,
                                hasIn: startSpatial.hasIn, inTangent: startSpatial.inTangent,
                                hasOut: true, outTangent: outTangent)
        core.setSpatialTangents(entityID: layerID, path: path, frame: endFrame,
                                hasIn: true, inTangent: inTangent,
                                hasOut: endSpatial.hasOut, outTangent: endSpatial.outTangent)
    }

    private func endDragIfNeeded() {
        guard isDragging else { return }
        isDragging = false
        core.endDrag()
        perform("Set Spatial Tangents") {}
    }
}
