//
//  EditorViewController+Commands.swift
//  MotionStudioApp
//
//  Editor commands and undo registration.
//

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

    @objc func addImageLayer() {
        let alert = UIAlertController(title: "Image Layers",
                                      message: "Image layer creation needs the bridge API before it can be wired here.",
                                      preferredStyle: .alert)
        alert.addAction(UIAlertAction(title: "OK", style: .default))
        present(alert, animated: true)
    }

    func clearSelection() {
        editorState.clearLayerSelection()
    }

    func canArrangeSelection(_ action: LayerArrangeAction) -> Bool {
        let compositionID = document.core.firstCompositionID
        let current = document.core.layerIDs(compositionID: compositionID)
        let moving = Set(editorState.selectedLayerIDs)
        return TimelineReorder.arrangedLayerIDs(current: current, moving: moving, action: action) != nil
    }

    func arrangeSelection(_ action: LayerArrangeAction, actionName: String) {
        let compositionID = document.core.firstCompositionID
        let current = document.core.layerIDs(compositionID: compositionID)
        let moving = Set(editorState.selectedLayerIDs)
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

    func perform(_ actionName: String, edit: () -> Void) {
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
