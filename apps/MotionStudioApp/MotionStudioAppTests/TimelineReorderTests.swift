//
//  TimelineReorderTests.swift
//  MotionStudioAppTests
//

import Foundation
@testable import MotionStudio
import Testing

struct TimelineReorderTests {
    @Test
    func `reordered layer I ds single layer to front`() {
        let current: [UInt64] = [1, 2, 3]
        let result = TimelineReorder.reorderedLayerIDs(current: current,
                                                       moving: Set<UInt64>([1]),
                                                       insertBeforeModelIndex: 3)
        #expect(result == [2, 3, 1] as [UInt64])
    }

    @Test
    func `reordered layer I ds non contiguous becomes block`() {
        let current: [UInt64] = [1, 2, 3, 4, 5]
        let result = TimelineReorder.reorderedLayerIDs(current: current,
                                                       moving: Set<UInt64>([1, 3]),
                                                       insertBeforeModelIndex: 4)
        #expect(result == [2, 4, 1, 3, 5] as [UInt64])
    }

    @Test
    func `model insert before index maps UI slots`() {
        #expect(TimelineReorder.modelInsertBeforeIndex(uiSlot: 0, layerCount: 4) == 4)
        #expect(TimelineReorder.modelInsertBeforeIndex(uiSlot: 4, layerCount: 4) == 0)
        #expect(TimelineReorder.modelInsertBeforeIndex(uiSlot: 1, layerCount: 4) == 3)
    }

    @Test
    func `move steps reaches target`() {
        let from: [UInt64] = [1, 2, 3]
        let to: [UInt64] = [2, 3, 1]
        var order = from
        for step in TimelineReorder.moveSteps(from: from, to: to) {
            let layer = order.remove(at: step.from)
            order.insert(layer, at: step.to)
        }
        #expect(order == to)
    }

    @Test
    func `move steps noop when equal`() {
        #expect(TimelineReorder.moveSteps(from: [1, 2], to: [1, 2]).isEmpty)
    }

    @Test
    func `arranged layer I ds bring forward and back`() {
        let current: [UInt64] = [1, 2, 3, 4]
        #expect(TimelineReorder.arrangedLayerIDs(current: current, moving: Set([2]),
                                                 action: .bringForward) == [1, 3, 2, 4] as [UInt64])
        #expect(TimelineReorder.arrangedLayerIDs(current: current, moving: Set([2]),
                                                 action: .sendBackward) == [2, 1, 3, 4] as [UInt64])
        #expect(TimelineReorder.arrangedLayerIDs(current: current, moving: Set([2]),
                                                 action: .bringToFront) == [1, 3, 4, 2] as [UInt64])
        #expect(TimelineReorder.arrangedLayerIDs(current: current, moving: Set([2]),
                                                 action: .sendToBack) == [2, 1, 3, 4] as [UInt64])
        #expect(TimelineReorder.arrangedLayerIDs(current: current, moving: Set([4]),
                                                 action: .bringToFront) == nil)
        #expect(TimelineReorder.arrangedLayerIDs(current: current, moving: Set([1]),
                                                 action: .sendToBack) == nil)
    }
}
