//
//  EditorViewController+Saving.swift
//  MotionStudioApp
//
//  UIKit document saving for the editor shell.
//

import UIKit

@MainActor
extension EditorViewController {
    var hasUnsavedChanges: Bool {
        document.isTemporaryDraft || document.hasUnsavedChanges
    }

    func initializeSaveStateIfNeeded() {
        updateSaveButtonState()
    }

    func updateSaveButtonState() {
        saveButton.tintColor = hasUnsavedChanges ? Palette.buttonTint : .secondaryLabel
        saveButton.backgroundColor = hasUnsavedChanges ? Palette.buttonBackground : .clear
        updateDocumentStatusView()
        updateWindowCloseAvailability()
        UIMenuSystem.main.setNeedsRevalidate()
    }

    func updateDocumentStatusView() {
        let projectName = document.saveURL.deletingPathExtension().lastPathComponent
        documentDirtyIndicator.isHidden = !hasUnsavedChanges
        documentStatusLabel.text = hasUnsavedChanges ? "\(projectName) · Modified" : projectName
        documentStatusLabel.textColor = hasUnsavedChanges ? .label : .secondaryLabel
    }

    @objc func saveCurrentDocument() {
        guard !document.isTemporaryDraft else {
            saveDocumentAs()
            return
        }

        saveDocument(to: document.saveURL, operation: .forOverwriting)
    }

    @objc func saveDocumentAs() {
        do {
            let url = try ProjectLibraryStore.makeProjectCopyURL(for: document.saveURL)
            saveDocument(to: url, operation: .forCreating)
        } catch {
            presentSaveError(error)
        }
    }

    func saveDocument(to url: URL, operation: UIDocument.SaveOperation, completion: ((Bool) -> Void)? = nil) {
        let shouldStopAccessing = url.startAccessingSecurityScopedResource()
        document.save(to: url, for: operation) { [weak self] success in
            guard let self else { return }
            if shouldStopAccessing {
                url.stopAccessingSecurityScopedResource()
            }
            if success {
                markSaved(at: url)
            } else {
                presentSaveError(CocoaError(.fileWriteUnknown))
            }
            completion?(success)
        }
    }

    func markSaved(at url: URL) {
        document.markSaved(to: url)
        ProjectLibraryStore.remember(url: url)
        title = url.deletingPathExtension().lastPathComponent
        updateSaveButtonState()
    }

    private func presentSaveError(_ error: Error) {
        let alert = UIAlertController(title: "Save Failed",
                                      message: error.localizedDescription,
                                      preferredStyle: .alert)
        alert.addAction(UIAlertAction(title: "OK", style: .default))
        present(alert, animated: true)
    }
}
