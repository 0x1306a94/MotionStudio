//
//  EditorViewController+Commands.swift
//  MotionStudioApp
//
//  Editor commands and undo registration.
//

import MotionStudioBridging
import UIKit

@MainActor
extension EditorViewController {
    func maximumTimelineHeight() -> CGFloat {
        max(Metrics.timelineMinimumHeight, ceil(view.bounds.height * Metrics.timelineMaximumFraction))
    }

    func clampTimelineHeight() {
        let clamped = min(max(timelineHeight, Metrics.timelineMinimumHeight), maximumTimelineHeight())
        guard clamped != timelineHeight || timelineHeightConstraint?.constant != clamped else { return }
        timelineHeight = clamped
        timelineHeightConstraint?.constant = clamped
    }

    @objc func handleTimelineResize(_ recognizer: UIPanGestureRecognizer) {
        switch recognizer.state {
        case .began:
            timelineDragStartHeight = timelineHeight
        case .changed, .ended:
            let nextHeight = timelineDragStartHeight - recognizer.translation(in: view).y
            timelineHeight = min(max(nextHeight, Metrics.timelineMinimumHeight), maximumTimelineHeight())
            timelineHeightConstraint?.constant = timelineHeight
        default:
            break
        }
    }

    @objc func togglePlayback() {
        editorState.isPlaying.toggle()
    }

    @objc func performUndoFromButton() {
        editorUndoManager.undo()
    }

    @objc func performRedoFromButton() {
        editorUndoManager.redo()
    }

    func observeUndoManagerNotifications() {
        let center = NotificationCenter.default
        center.addObserver(self,
                           selector: #selector(updateUndoButtonStates),
                           name: .NSUndoManagerDidCloseUndoGroup,
                           object: editorUndoManager)
        center.addObserver(self,
                           selector: #selector(updateUndoButtonStates),
                           name: .NSUndoManagerDidUndoChange,
                           object: editorUndoManager)
        center.addObserver(self,
                           selector: #selector(updateUndoButtonStates),
                           name: .NSUndoManagerDidRedoChange,
                           object: editorUndoManager)
    }

    @objc func updateUndoButtonStates() {
        undoButton.isEnabled = editorUndoManager.canUndo
        redoButton.isEnabled = editorUndoManager.canRedo
    }

    @objc func addRectangleLayer() {
        let compositionID = document.core.firstCompositionID
        perform("Add Rectangle") {
            editorState.selectedLayerID = document.core.addRectLayer(compositionID: compositionID)
        }
    }

    @objc func addEllipseLayer() {
        let compositionID = document.core.firstCompositionID
        perform("Add Ellipse") {
            editorState.selectedLayerID = document.core.addEllipseLayer(compositionID: compositionID)
        }
    }

    @objc func activateSelectTool() {
        guard editorState.tool != .select else {
            return
        }
        finishPenTool()
    }

    @objc func toggleSelectionAnchor() {
        editorState.showSelectionAnchor.toggle()
    }

    @objc func activatePenTool() {
        guard editorState.tool != .pen else {
            return
        }
        editorState.tool = .pen
        activatePenTargetFromSelection()
    }

    @objc func togglePenTool() {
        if editorState.tool == .pen {
            finishPenTool()
            return
        }
        activatePenTool()
    }

    @objc func exitPenTool() {
        guard editorState.tool == .pen else {
            return
        }
        if var target = editorState.pathEditTarget, target.activeVertexId != 0 {
            target.clearVertexSelection()
            editorState.pathEditTarget = target
            return
        }
        finishPenTool()
    }

    private func finishPenTool() {
        if let target = editorState.pathEditTarget, target.kind == .SHAPE {
            let revision = document.core.revision
            document.core.pathEditRecenterShape(layerID: target.layerID,
                                                frame: playheadClock.frame)
            if document.core.revision != revision {
                registerEdit("Center Path Anchor")
            }
        }
        editorState.clearPathEdit()
    }

    private var canDeletePathVertex: Bool {
        guard editorState.tool == .pen,
              let target = editorState.pathEditTarget
        else {
            return false
        }
        return target.activeVertexId != 0
    }

    func canDeleteSelection() -> Bool {
        canDeletePathVertex || !editorState.selectedLayerIDs.isEmpty
    }

    /// System Edit → Delete, Backspace / Delete key. Pen vertex takes priority.
    override func delete(_: Any?) {
        if canDeletePathVertex {
            deletePathVertex()
            return
        }
        deleteSelectedLayers()
    }

    @objc func deletePathVertex() {
        guard canDeletePathVertex,
              let target = editorState.pathEditTarget
        else {
            return
        }
        let vertexId = target.activeVertexId
        perform("Delete Vertex") {
            document.core.networkEditRemoveVertex(layerID: target.layerID, kind: target.kind,
                                                  maskIndex: target.maskIndex,
                                                  frame: playheadClock.frame,
                                                  vertexId: vertexId)
        }
        var next = target
        next.clearVertexSelection()
        editorState.pathEditTarget = next
    }

    func deleteSelectedLayers() {
        let layerIDs = editorState.selectedLayerIDs
        guard !layerIDs.isEmpty else {
            return
        }
        let compositionID = document.core.firstCompositionID
        let deleted = Set(layerIDs)
        let actionName = layerIDs.count > 1 ? "Delete Layers" : "Delete Layer"
        perform(actionName) {
            document.core.removeLayers(compositionID: compositionID, layerIDs: layerIDs)
            editorState.clearLayerSelection()
            if let target = editorState.pathEditTarget, deleted.contains(target.layerID) {
                editorState.clearPathEdit()
            }
        }
    }

    func activatePenTargetFromSelection() {
        guard let layerID = editorState.selectedLayerID else {
            editorState.pathEditTarget = nil
            return
        }
        // Toolbar / double-click always target the shape path. Mask edit stays
        // Inspector-only (`onEditMaskPath`).
        if !document.core.hasBezierPath(entityID: layerID, path: ShapeProperty.path.path) {
            perform("Convert to Path") {
                document.core.convertGeometryToPath(layerID: layerID, frame: playheadClock.frame)
            }
        }
        if document.core.hasBezierPath(entityID: layerID, path: ShapeProperty.path.path) {
            editorState.pathEditTarget = .shape(layerID: layerID)
            editorState.selectedLayerID = layerID
        } else {
            editorState.pathEditTarget = nil
        }
    }

    @objc func addImageLayer() {
        let compositionID = document.core.firstCompositionID
        document.syncProjectRoot()
        perform("Add Image Layer") {
            editorState.selectedLayerID = document.core.addImageLayer(compositionID: compositionID)
        }
    }

    @objc func addTextLayer() {
        let compositionID = document.core.firstCompositionID
        perform("Add Text Layer") {
            editorState.selectedLayerID = document.core.addTextLayer(compositionID: compositionID)
        }
    }

    func presentImageImport() {
        if imageImportCoordinator == nil {
            imageImportCoordinator = ImageImportCoordinator(
                presenter: self,
                document: document,
                perform: { [weak self] name, edit in
                    self?.perform(name, edit: edit)
                },
            )
        }
        imageImportCoordinator?.presentImport()
    }

    func clearSelection() {
        editorState.clearLayerSelection()
    }

    func canArrangeSelection(_ action: LayerArrangeAction) -> Bool {
        let compositionID = document.core.firstCompositionID
        let current = document.core.layerIDs(compositionID: compositionID)
        var parentOf: [UInt64: UInt64] = [:]
        for id in current {
            parentOf[id] = document.core.layerParentID(id)
        }
        let moving = TimelineLayerTree.movingIDsIncludingDescendants(
            order: current, parentOf: parentOf, moving: Set(editorState.selectedLayerIDs),
        )
        return TimelineReorder.arrangedLayerIDs(current: current, moving: moving, action: action) != nil
    }

    func arrangeSelection(_ action: LayerArrangeAction, actionName: String) {
        let compositionID = document.core.firstCompositionID
        let current = document.core.layerIDs(compositionID: compositionID)
        var parentOf: [UInt64: UInt64] = [:]
        for id in current {
            parentOf[id] = document.core.layerParentID(id)
        }
        let moving = TimelineLayerTree.movingIDsIncludingDescendants(
            order: current, parentOf: parentOf, moving: Set(editorState.selectedLayerIDs),
        )
        guard let desired = TimelineReorder.arrangedLayerIDs(current: current, moving: moving,
                                                             action: action)
        else {
            return
        }
        perform(actionName) {
            document.core.applyLayerOrder(compositionID: compositionID, desired: desired)
        }
    }

    @objc func bringLayersToFront() {
        arrangeSelection(.bringToFront, actionName: "Bring to Front")
    }

    @objc func bringLayersForward() {
        arrangeSelection(.bringForward, actionName: "Bring Forward")
    }

    @objc func sendLayersBackward() {
        arrangeSelection(.sendBackward, actionName: "Send Backward")
    }

    @objc func sendLayersToBack() {
        arrangeSelection(.sendToBack, actionName: "Send to Back")
    }

    @objc func nudgeSelectionLeft() {
        nudgeSelection(dx: -1, dy: 0)
    }

    @objc func nudgeSelectionRight() {
        nudgeSelection(dx: 1, dy: 0)
    }

    @objc func nudgeSelectionUp() {
        nudgeSelection(dx: 0, dy: -1)
    }

    @objc func nudgeSelectionDown() {
        nudgeSelection(dx: 0, dy: 1)
    }

    func nudgeSelection(dx: CGFloat, dy: CGFloat) {
        let layerIDs = editorState.selectedLayerIDs.filter { !document.core.layerIsEffectivelyLocked($0) }
        guard !layerIDs.isEmpty else { return }
        let compositionID = document.core.firstCompositionID
        let frame = playheadClock.frame
        // Keep one merge group across rapid key repeats (same idea as Core's
        // 500ms SetStatic merge window used by Inspector step buttons). Closing
        // the group on every keypress made each arrow its own Undo step.
        let continuing = isNudgeMergeActive
        if !continuing {
            document.core.beginMergeGroup()
            isNudgeMergeActive = true
        }
        document.core.nudgeLayersPosition(compositionID: compositionID,
                                          layerIDs: layerIDs,
                                          delta: CGVector(dx: dx, dy: dy),
                                          frame: frame)
        if continuing {
            document.markEdited()
            updateSaveButtonState()
        } else {
            registerEdit("Nudge Position")
        }
        scheduleNudgeMergeEnd()
    }

    /// Matches `UndoManager` default merge window (500ms).
    private static let nudgeMergeIdleInterval: TimeInterval = 0.5

    func scheduleNudgeMergeEnd() {
        nudgeMergeEndWorkItem?.cancel()
        let work = DispatchWorkItem { [weak self] in
            self?.endNudgeMergeIfNeeded()
        }
        nudgeMergeEndWorkItem = work
        DispatchQueue.main.asyncAfter(deadline: .now() + Self.nudgeMergeIdleInterval, execute: work)
    }

    func endNudgeMergeIfNeeded() {
        nudgeMergeEndWorkItem?.cancel()
        nudgeMergeEndWorkItem = nil
        guard isNudgeMergeActive else { return }
        document.core.endMergeGroup()
        isNudgeMergeActive = false
    }

    @objc func alignSelectionLeft() {
        alignSelection(edge: .left, name: "Align Left")
    }

    @objc func alignSelectionHorizontalCenter() {
        alignSelection(edge: .horizontalCenter, name: "Align Horizontal Centers")
    }

    @objc func alignSelectionRight() {
        alignSelection(edge: .right, name: "Align Right")
    }

    @objc func alignSelectionTop() {
        alignSelection(edge: .top, name: "Align Top")
    }

    @objc func alignSelectionVerticalCenter() {
        alignSelection(edge: .verticalCenter, name: "Align Vertical Centers")
    }

    @objc func alignSelectionBottom() {
        alignSelection(edge: .bottom, name: "Align Bottom")
    }

    func alignSelection(edge: LayerAlignEdge, name: String) {
        let layerIDs = editorState.selectedLayerIDs.filter { !document.core.layerIsEffectivelyLocked($0) }
        guard !layerIDs.isEmpty else { return }
        let compositionID = document.core.firstCompositionID
        let frame = playheadClock.frame
        perform(name) {
            document.core.beginMergeGroup()
            document.core.alignLayers(compositionID: compositionID,
                                      layerIDs: layerIDs,
                                      edge: edge,
                                      frame: frame)
            document.core.endMergeGroup()
        }
    }

    func perform(_ actionName: String, edit: () -> Void) {
        endNudgeMergeIfNeeded()
        edit()
        registerEdit(actionName)
    }

    func registerEdit(_ actionName: String) {
        document.markEdited()
        updateSaveButtonState()
        guard let undoManager = currentUndoManager() else { return }
        undoManager.setActionName(actionName)
        registerInverse(redo: false, undoManager: undoManager)
    }

    private func currentUndoManager() -> UndoManager? {
        becomeFirstResponder()
        return undoManager
    }

    func registerInverse(redo: Bool, undoManager: UndoManager) {
        undoManager.registerUndo(withTarget: document.core) { [weak self] core in
            if redo {
                core.performRedo()
            } else {
                core.performUndo()
            }
            guard let self else { return }
            document.markEdited()
            updateSaveButtonState()
            registerInverse(redo: !redo, undoManager: undoManager)
        }
    }
}
