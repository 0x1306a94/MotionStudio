//
//  EditorViewController+Renaming.swift
//  MotionStudioApp
//
//  Project renaming for the active editor document.
//

import UIKit

@MainActor
extension EditorViewController {
    @objc func renameCurrentProject() {
        let alert = UIAlertController(title: "Rename Project",
                                      message: nil,
                                      preferredStyle: .alert)
        alert.addTextField { [document] textField in
            textField.text = document.saveURL.deletingPathExtension().lastPathComponent
            textField.clearButtonMode = .whileEditing
        }
        alert.addAction(UIAlertAction(title: "Cancel", style: .cancel))
        alert.addAction(UIAlertAction(title: "Rename", style: .default) { [weak self, weak alert] _ in
            guard let name = alert?.textFields?.first?.text else { return }
            self?.commitCurrentProjectRename(name: name)
        })
        present(alert, animated: true)
    }

    private func commitCurrentProjectRename(name: String) {
        if hasUnsavedChanges {
            saveDocument(to: document.saveURL, operation: .forOverwriting) { [weak self] success in
                guard success else { return }
                self?.moveCurrentProject(to: name)
            }
        } else {
            moveCurrentProject(to: name)
        }
    }

    private func moveCurrentProject(to name: String) {
        do {
            let renamedURL = try ProjectLibraryStore.rename(url: document.saveURL, to: name)
            document.markSaved(to: renamedURL)
            title = renamedURL.deletingPathExtension().lastPathComponent
            updateSaveButtonState()
        } catch {
            presentRenameError(error)
        }
    }

    private func presentRenameError(_ error: Error) {
        let alert = UIAlertController(title: "Rename Failed",
                                      message: error.localizedDescription,
                                      preferredStyle: .alert)
        alert.addAction(UIAlertAction(title: "OK", style: .default))
        present(alert, animated: true)
    }
}
