//
//  EditorViewController+Closing.swift
//  MotionStudioApp
//
//  Close handling for document-backed editor windows.
//

import UIKit

@MainActor
extension EditorViewController {
    @objc func requestCloseWindow() {
        guard hasUnsavedChanges else {
            closeWindowScene()
            return
        }

        let alert = UIAlertController(title: "Save Changes?",
                                      message: "Save changes to \(title ?? "this project") before closing?",
                                      preferredStyle: .alert)
        alert.addAction(UIAlertAction(title: "Save", style: .default) { [weak self] _ in
            self?.saveAndCloseWindow()
        })
        alert.addAction(UIAlertAction(title: "Discard", style: .destructive) { [weak self] _ in
            self?.closeWindowScene()
        })
        alert.addAction(UIAlertAction(title: "Cancel", style: .cancel))
        present(alert, animated: true)
    }

    func saveBeforeSceneDisconnect() {
        stopPreviewPlayback()
        guard hasUnsavedChanges else {
            return
        }
        document.save(to: document.saveURL, for: .forOverwriting, completionHandler: nil)
    }

    func stopPreviewPlayback() {
        editorState.isPlaying = false
        canvasViewController?.shutdown()
    }

    func updateWindowCloseAvailability() {
        #if targetEnvironment(macCatalyst)
            view.window?.windowScene?.windowingBehaviors?.isClosable = !hasUnsavedChanges
        #endif
    }

    private func saveAndCloseWindow() {
        saveDocument(to: document.saveURL, operation: .forOverwriting) { [weak self] success in
            guard success else {
                return
            }
            self?.closeWindowScene()
        }
    }

    private func closeWindowScene() {
        stopPreviewPlayback()
        guard let sceneSession = view.window?.windowScene?.session else {
            return
        }
        UIApplication.shared.requestSceneSessionDestruction(sceneSession, options: nil)
    }
}
