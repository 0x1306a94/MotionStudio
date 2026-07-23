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
        editorState.selectedLayerID = nil
        editorState.selectedTimelineProperty = nil
        editorState.selectedTimelineSegment = nil
    }

    func perform(_ actionName: String, edit: () -> Void) {
        edit()
        registerEdit(actionName)
    }

    func registerEdit(_ actionName: String) {
        guard let undoManager else { return }
        undoManager.setActionName(actionName)
        registerInverse(redo: false, undoManager: undoManager)
        document.markEdited()
        updateSaveButtonState()
    }

    func registerInverse(redo: Bool, undoManager: UndoManager) {
        undoManager.registerUndo(withTarget: document.core) { core in
            if redo {
                core.performRedo()
            } else {
                core.performUndo()
            }
            self.document.markEdited()
            self.updateSaveButtonState()
            self.registerInverse(redo: !redo, undoManager: undoManager)
        }
    }
}
