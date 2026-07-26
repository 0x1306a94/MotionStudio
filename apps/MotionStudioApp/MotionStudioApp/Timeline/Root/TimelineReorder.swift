//
//  TimelineReorder.swift
//  MotionStudioApp
//
//  Pure helpers for layer draw-order reordering (model order: bottom → top).
//

import CoreGraphics
import Foundation

enum LayerArrangeAction {
    case bringToFront
    case bringForward
    case sendBackward
    case sendToBack

    var actionName: String {
        switch self {
        case .bringToFront: "Bring to Front"
        case .bringForward: "Bring Forward"
        case .sendBackward: "Send Backward"
        case .sendToBack: "Send to Back"
        }
    }
}

struct LayerBlockFrame: Equatable {
    let layerID: UInt64
    let minY: CGFloat
    let maxY: CGFloat
}

enum TimelineReorder {
    /// UI insert slot → model `insertBefore` index used by `reorderedLayerIDs`.
    /// `uiSlot`: 0 = above the frontmost layer, `layerCount` = below the backmost.
    nonisolated static func modelInsertBeforeIndex(uiSlot: Int, layerCount: Int) -> Int {
        let clamped = min(max(0, uiSlot), layerCount)
        return layerCount - clamped
    }

    /// Extracts `moving` in model order, then inserts that block into the remaining
    /// layers. `insertBeforeModelIndex` is interpreted against the full `current`
    /// array: non-moving layers before that index determine the insert offset in
    /// `remaining`.
    nonisolated static func reorderedLayerIDs(current: [UInt64], moving: Set<UInt64>,
                                              insertBeforeModelIndex: Int) -> [UInt64]
    {
        guard !moving.isEmpty else {
            return current
        }
        let movingOrdered = current.filter { moving.contains($0) }
        guard !movingOrdered.isEmpty else {
            return current
        }
        let remaining = current.filter { !moving.contains($0) }
        let clampedBefore = min(max(0, insertBeforeModelIndex), current.count)
        let insertAt = current.prefix(clampedBefore).count(where: { !moving.contains($0) })
        var result = remaining
        result.insert(contentsOf: movingOrdered, at: min(insertAt, remaining.count))
        return result
    }

    nonisolated static func moveSteps(from: [UInt64], to: [UInt64]) -> [(from: Int, to: Int)] {
        guard from.count == to.count, Set(from) == Set(to) else {
            return []
        }
        var order = from
        var steps: [(from: Int, to: Int)] = []
        for targetIndex in to.indices {
            let wanted = to[targetIndex]
            guard let sourceIndex = order.firstIndex(of: wanted) else {
                return []
            }
            if sourceIndex == targetIndex {
                continue
            }
            let layer = order.remove(at: sourceIndex)
            order.insert(layer, at: targetIndex)
            steps.append((sourceIndex, targetIndex))
        }
        return steps
    }

    nonisolated static func arrangedLayerIDs(current: [UInt64], moving: Set<UInt64>,
                                             action: LayerArrangeAction) -> [UInt64]?
    {
        guard !moving.isEmpty else {
            return nil
        }
        let movingOrdered = current.filter { moving.contains($0) }
        guard !movingOrdered.isEmpty else {
            return nil
        }
        let remaining = current.filter { !moving.contains($0) }
        guard let firstMovingIndex = current.firstIndex(where: { moving.contains($0) }) else {
            return nil
        }
        let currentInsertAt = current.prefix(firstMovingIndex).count(where: { !moving.contains($0) })
        let targetInsertAt: Int = switch action {
        case .bringToFront:
            remaining.count
        case .sendToBack:
            0
        case .bringForward:
            min(currentInsertAt + 1, remaining.count)
        case .sendBackward:
            max(currentInsertAt - 1, 0)
        }
        var desired = remaining
        desired.insert(contentsOf: movingOrdered, at: targetInsertAt)
        return desired == current ? nil : desired
    }

    /// Builds top→bottom block frames for the UI row list (already reversed model order).
    static func layerBlockFrames(rows: [TimelineRow]) -> [LayerBlockFrame] {
        var frames: [LayerBlockFrame] = []
        var y: CGFloat = 0
        var index = 0
        while index < rows.count {
            let row = rows[index]
            guard case .layer = row.kind else {
                y += row.height
                index += 1
                continue
            }
            let layerID = row.layerID
            let minY = y
            y += row.height
            index += 1
            while index < rows.count {
                let next = rows[index]
                if case .layer = next.kind {
                    break
                }
                guard next.layerID == layerID else {
                    break
                }
                y += next.height
                index += 1
            }
            frames.append(LayerBlockFrame(layerID: layerID, minY: minY, maxY: y))
        }
        return frames
    }

    static func uiInsertSlot(y: CGFloat, frames: [LayerBlockFrame]) -> Int {
        guard !frames.isEmpty else {
            return 0
        }
        for (index, frame) in frames.enumerated() {
            let midpoint = (frame.minY + frame.maxY) * 0.5
            if y < midpoint {
                return index
            }
        }
        return frames.count
    }
}
