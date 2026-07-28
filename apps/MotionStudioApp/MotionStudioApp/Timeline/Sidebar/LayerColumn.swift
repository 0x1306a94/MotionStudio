//
//  LayerColumn.swift
//  MotionStudioApp
//

import SwiftUI

private struct LayerReorderDragState {
    var movingIDs: Set<UInt64>
    var startOrder: [UInt64]
    var lastDesired: [UInt64]
    var frozenFrames: [LayerBlockFrame]
    var insertionUISlot: Int?
    var lastViewportY: CGFloat
    var lastDragLayerID: UInt64
}

struct LayerColumn: View {
    @Environment(MotionDocumentCore.self) private var core
    @Environment(EditorState.self) private var editorState

    let rows: [TimelineRow]
    let perform: (String, () -> Void) -> Void
    let registerEdit: (String) -> Void
    let clearSelection: () -> Void
    let verticalScroller: TimelineVerticalScroller

    @State private var drag: LayerReorderDragState?
    @State private var autoScrollTask: Task<Void, Never>?

    var body: some View {
        let _ = core.revision
        ZStack(alignment: .topLeading) {
            Color.clear
                .contentShape(Rectangle())
                .onTapGesture(perform: clearSelection)
            VStack(spacing: 0) {
                ForEach(rows) { row in
                    switch row.kind {
                    case .layer:
                        LayerRow(layerID: row.layerID,
                                 perform: perform,
                                 arrange: arrangeFromContextMenu,
                                 deleteLayers: deleteFromContextMenu,
                                 onReorderDragChanged: handleReorderDragChanged,
                                 onReorderDragEnded: handleReorderDragEnded)
                            .frame(height: row.height)
                            .opacity(dragOpacity(for: row.layerID))
                    case let .propertySpan(path, label), let .keyframeTrack(path, label):
                        PropertySubRow(layerID: row.layerID,
                                       label: label,
                                       path: path)
                            .frame(height: row.height)
                            .opacity(dragOpacity(for: row.layerID))
                    }
                }
            }

            if let slot = drag?.insertionUISlot, let frames = drag?.frozenFrames {
                insertionLine(slot: slot, frames: frames)
            }
        }
        .coordinateSpace(name: layerColumnCoordinateSpace)
        .frame(maxHeight: .infinity, alignment: .topLeading)
        .onDisappear {
            stopAutoScroll()
        }
    }

    private func dragOpacity(for layerID: UInt64) -> Double {
        guard let moving = drag?.movingIDs, moving.contains(layerID) else {
            return 1
        }
        return 0.55
    }

    @ViewBuilder
    private func insertionLine(slot: Int, frames: [LayerBlockFrame]) -> some View {
        let y: CGFloat = {
            if frames.isEmpty {
                return 0
            }
            if slot <= 0 {
                return frames[0].minY
            }
            if slot >= frames.count {
                return frames[frames.count - 1].maxY
            }
            return frames[slot].minY
        }()
        Rectangle()
            .fill(Color.accentColor)
            .frame(height: 2)
            .frame(maxWidth: .infinity)
            .offset(y: y - 1)
            .allowsHitTesting(false)
    }

    private func contentY(fromViewportY viewportY: CGFloat) -> CGFloat {
        let insetTop = verticalScroller.scrollView?.adjustedContentInset.top ?? 0
        return viewportY + verticalScroller.contentOffsetY + insetTop
    }

    private func handleReorderDragChanged(layerID: UInt64, viewportY: CGFloat) {
        let compositionID = core.firstCompositionID
        if drag == nil {
            let moving: Set<UInt64>
            if editorState.isLayerSelected(layerID) {
                moving = Set(editorState.selectedLayerIDs)
            } else {
                editorState.selectLayer(layerID)
                moving = [layerID]
            }
            let startOrder = core.layerIDs(compositionID: compositionID)
            core.beginDrag()
            drag = LayerReorderDragState(movingIDs: moving,
                                         startOrder: startOrder,
                                         lastDesired: startOrder,
                                         frozenFrames: TimelineReorder.layerBlockFrames(rows: rows),
                                         insertionUISlot: nil,
                                         lastViewportY: viewportY,
                                         lastDragLayerID: layerID)
        }
        guard var state = drag else {
            return
        }
        state.lastViewportY = viewportY
        state.lastDragLayerID = layerID
        drag = state
        applyReorder(using: state, contentY: contentY(fromViewportY: viewportY))
        updateAutoScroll(viewportY: viewportY)
    }

    private func applyReorder(using state: LayerReorderDragState, contentY: CGFloat) {
        let compositionID = core.firstCompositionID
        let slot = TimelineReorder.uiInsertSlot(y: contentY, frames: state.frozenFrames)
        let insertBefore = TimelineReorder.modelInsertBeforeIndex(uiSlot: slot,
                                                                  layerCount: state.startOrder.count)
        let desired = TimelineReorder.reorderedLayerIDs(current: state.startOrder,
                                                        moving: state.movingIDs,
                                                        insertBeforeModelIndex: insertBefore)
        var next = state
        next.insertionUISlot = slot
        if desired != next.lastDesired {
            core.applyLayerOrder(compositionID: compositionID, desired: desired)
            next.lastDesired = desired
        }
        drag = next
    }

    private func updateAutoScroll(viewportY: CGFloat) {
        let viewportHeight = max(verticalScroller.viewportHeight, 1)
        let edge = layerReorderAutoScrollEdge
        let speed: CGFloat
        if viewportY < edge {
            let intensity = 1 - (viewportY / edge)
            speed = -layerReorderAutoScrollMaxSpeed * max(0, min(1, intensity))
        } else if viewportY > viewportHeight - edge {
            let intensity = (viewportY - (viewportHeight - edge)) / edge
            speed = layerReorderAutoScrollMaxSpeed * max(0, min(1, intensity))
        } else {
            stopAutoScroll()
            return
        }
        if abs(speed) < 1 {
            stopAutoScroll()
            return
        }
        startAutoScrollIfNeeded()
    }

    private func startAutoScrollIfNeeded() {
        if autoScrollTask != nil {
            // Keep existing loop; speed is read from latest drag viewport each tick.
            return
        }
        autoScrollTask = Task { @MainActor in
            let frameNanoseconds: UInt64 = 16_666_667
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: frameNanoseconds)
                guard !Task.isCancelled, let state = drag else {
                    break
                }
                let viewportHeight = max(verticalScroller.viewportHeight, 1)
                let viewportY = state.lastViewportY
                let edge = layerReorderAutoScrollEdge
                let speed: CGFloat
                if viewportY < edge {
                    let intensity = 1 - (viewportY / edge)
                    speed = -layerReorderAutoScrollMaxSpeed * max(0, min(1, intensity))
                } else if viewportY > viewportHeight - edge {
                    let intensity = (viewportY - (viewportHeight - edge)) / edge
                    speed = layerReorderAutoScrollMaxSpeed * max(0, min(1, intensity))
                } else {
                    break
                }
                if abs(speed) < 1 {
                    break
                }
                let before = verticalScroller.contentOffsetY
                verticalScroller.scrollBy(speed * (1.0 / 60.0))
                let after = verticalScroller.contentOffsetY
                if before == after {
                    break
                }
                applyReorder(using: state, contentY: contentY(fromViewportY: viewportY))
            }
            autoScrollTask = nil
        }
    }

    private func stopAutoScroll() {
        autoScrollTask?.cancel()
        autoScrollTask = nil
    }

    private func handleReorderDragEnded() {
        stopAutoScroll()
        guard let state = drag else {
            return
        }
        core.endDrag()
        if state.lastDesired != state.startOrder {
            registerEdit(state.movingIDs.count > 1 ? "Move Layers" : "Move Layer")
        }
        drag = nil
    }

    private func arrangeFromContextMenu(layerID: UInt64, action: LayerArrangeAction) {
        if !editorState.isLayerSelected(layerID) {
            editorState.selectLayer(layerID)
        }
        let compositionID = core.firstCompositionID
        let current = core.layerIDs(compositionID: compositionID)
        let moving = Set(editorState.selectedLayerIDs)
        guard let desired = TimelineReorder.arrangedLayerIDs(current: current, moving: moving,
                                                             action: action)
        else {
            return
        }
        perform(action.actionName) {
            core.applyLayerOrder(compositionID: compositionID, desired: desired)
        }
    }

    private func deleteFromContextMenu(layerID: UInt64) {
        if !editorState.isLayerSelected(layerID) {
            editorState.selectLayer(layerID)
        }
        let layerIDs = editorState.selectedLayerIDs
        guard !layerIDs.isEmpty else {
            return
        }
        let compositionID = core.firstCompositionID
        let deleted = Set(layerIDs)
        let actionName = layerIDs.count > 1 ? "Delete Layers" : "Delete Layer"
        perform(actionName) {
            core.removeLayers(compositionID: compositionID, layerIDs: layerIDs)
            editorState.clearLayerSelection()
            if let target = editorState.pathEditTarget, deleted.contains(target.layerID) {
                editorState.clearPathEdit()
            }
        }
    }
}
