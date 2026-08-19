//
//  TimelineReorderTests.swift
//  MotionStudioAppTests
//

import Foundation
@testable import MotionStudio
import MotionStudioBridging
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
    func `ui insert slot from drop row index`() {
        let rows = [
            TimelineRow(id: .layer(10), layerID: 10, kind: .layer),
            TimelineRow(id: .propertySpan(10, "a"), layerID: 10, kind: .propertySpan(path: "a", label: "A")),
            TimelineRow(id: .layer(20), layerID: 20, kind: .layer),
            TimelineRow(id: .layer(30), layerID: 30, kind: .layer),
        ]
        #expect(TimelineReorder.uiInsertSlot(dropBeforeRow: 0, rows: rows) == 0)
        #expect(TimelineReorder.uiInsertSlot(dropBeforeRow: 1, rows: rows) == 1)
        #expect(TimelineReorder.uiInsertSlot(dropBeforeRow: 2, rows: rows) == 1)
        #expect(TimelineReorder.uiInsertSlot(dropBeforeRow: 3, rows: rows) == 2)
        #expect(TimelineReorder.uiInsertSlot(dropBeforeRow: 4, rows: rows) == 3)
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

    @Test
    func `parent depth follows ancestor chain`() {
        let parentOf: [UInt64: UInt64] = [2: 1, 3: 2]
        #expect(TimelineLayerTree.parentDepth(layerID: 1, parentOf: parentOf) == 0)
        #expect(TimelineLayerTree.parentDepth(layerID: 2, parentOf: parentOf) == 1)
        #expect(TimelineLayerTree.parentDepth(layerID: 3, parentOf: parentOf) == 2)
    }

    @Test
    func `leading inset adds indent per depth`() {
        #expect(TimelineLayerTree.leadingInset(depth: 1, isProperty: false) == 20)
        #expect(TimelineLayerTree.leadingInset(depth: 1, isProperty: true) == 40)
    }

    @Test
    func `moving I ds include group descendants`() {
        let order: [UInt64] = [1, 2, 3]
        let parentOf: [UInt64: UInt64] = [2: 1, 3: 1]
        #expect(TimelineLayerTree.movingIDsIncludingDescendants(
            order: order, parentOf: parentOf, moving: Set([1]),
        ) == Set([1, 2, 3] as [UInt64]))
        #expect(TimelineLayerTree.movingIDsIncludingDescendants(
            order: order, parentOf: parentOf, moving: Set([2]),
        ) == Set([2] as [UInt64]))
    }

    @Test
    func `grouping I ds strip nested descendants`() {
        let parentOf: [UInt64: UInt64] = [2: 1, 3: 1]
        #expect(TimelineLayerTree.groupingIDsStrippingNested(
            ids: [1, 2], parentOf: parentOf,
        ) == [1] as [UInt64])
    }

    @Test
    func `can group requires shared parent`() {
        #expect(TimelineLayerTree.canGroup(ids: [2, 4], parentOf: [2: 1, 4: 5]) == false)
        #expect(TimelineLayerTree.canGroup(ids: [2, 3], parentOf: [2: 1, 3: 1]) == true)
    }

    @Test
    func `can ungroup when a group is selected`() {
        let types: [UInt64: MS_LAYER] = [1: .GROUP, 2: .SHAPE]
        #expect(TimelineLayerTree.canUngroup(ids: [1], types: types) == true)
        #expect(TimelineLayerTree.canUngroup(ids: [2], types: types) == false)
    }
}
