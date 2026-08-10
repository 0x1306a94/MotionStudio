//
//  EditorViewController+PagExport.swift
//  MotionStudioApp
//
//  File → Export PAG… / iPad Export menu: settings → progress → share / Files.
//

import UIKit

@MainActor
extension EditorViewController {
    var isExportInProgress: Bool {
        videoExportSession != nil || pagExportSession != nil
    }

    @objc func exportPAG() {
        guard !isExportInProgress else { return }

        if editorState.isPlaying {
            editorState.isPlaying = false
        }

        let core = document.core
        let compositionID = core.firstCompositionID
        guard compositionID != 0 else {
            presentExportAlert(title: "Export Failed", message: "No composition to export.")
            return
        }

        let size = core.size(compositionID: compositionID)
        let duration = core.duration(compositionID: compositionID)
        let fps = core.frameRate(compositionID: compositionID)
        let width = Int(size.width.rounded())
        let height = Int(size.height.rounded())
        let fpsText = fps == floor(fps) ? String(Int(fps)) : String(format: "%.2f", fps)
        let summary = "\(width)×\(height) · \(duration) frames · \(fpsText) fps"

        let settings = PagExportSettingsViewController(summary: summary, durationFrames: duration)
        settings.onExport = { [weak self] settings in
            self?.dismiss(animated: true) {
                self?.beginPagExport(settings: settings, compositionID: compositionID)
            }
        }
        settings.onCancel = { [weak self] in
            self?.dismiss(animated: true)
        }

        let navigation = UINavigationController(rootViewController: settings)
        navigation.modalPresentationStyle = .formSheet
        present(navigation, animated: true)
    }

    func beginPagExport(settings: PagExportSettings, compositionID: UInt64) {
        let session = PagExportSession()
        pagExportSession = session

        let projectName = document.isTemporaryDraft
            ? "Untitled"
            : document.saveURL.deletingPathExtension().lastPathComponent

        let outputURL: URL
        do {
            outputURL = try session.prepareOutputURL(projectName: projectName)
        } catch {
            pagExportSession = nil
            updateExportButtonState()
            presentExportAlert(title: "Export Failed", message: error.localizedDescription)
            return
        }

        let progressVC = PagExportProgressViewController()
        progressVC.modalPresentationStyle = .formSheet
        // PAG encode is synchronous and usually fast; Cancel only helps before Task starts.
        progressVC.onCancel = { [weak self] in
            self?.pagExportSession?.markDiscarded()
            self?.finishPagExportCancelled(progressVC: progressVC)
        }

        let core = document.core
        let outputPath = outputURL.path(percentEncoded: false)
        let allowBitmapExport = settings.allowBitmapExport
        updateExportButtonState()
        UIMenuSystem.main.setNeedsRevalidate()

        present(progressVC, animated: true) { [weak self] in
            guard let self else { return }
            Task.detached(priority: .utility) {
                do {
                    try core.exportPAG(compositionID: compositionID,
                                       outputPath: outputPath,
                                       allowBitmapExport: allowBitmapExport)
                    await MainActor.run {
                        guard self.pagExportSession?.isDiscarded != true else { return }
                        self.finishPagExportSuccess(outputURL: outputURL, progressVC: progressVC)
                    }
                } catch let PagExportError.failed(message) {
                    await MainActor.run {
                        guard self.pagExportSession?.isDiscarded != true else { return }
                        self.finishPagExportFailed(message: message, progressVC: progressVC)
                    }
                } catch {
                    await MainActor.run {
                        guard self.pagExportSession?.isDiscarded != true else { return }
                        self.finishPagExportFailed(message: error.localizedDescription, progressVC: progressVC)
                    }
                }
            }
        }
    }

    private func finishPagExportSuccess(outputURL: URL, progressVC _: UIViewController) {
        dismiss(animated: true) { [weak self] in
            guard let self else { return }
            #if targetEnvironment(macCatalyst)
                documentPickerPurpose = .exportPAG
                let picker = UIDocumentPickerViewController(forExporting: [outputURL], asCopy: true)
                picker.delegate = self
                picker.shouldShowFileExtensions = true
                present(picker, animated: true)
            #else
                presentPagExportShareSheet(for: outputURL)
            #endif
        }
    }

    #if !targetEnvironment(macCatalyst)
        private func presentPagExportShareSheet(for outputURL: URL) {
            let activity = UIActivityViewController(activityItems: [outputURL], applicationActivities: nil)
            activity.completionWithItemsHandler = { [weak self] _, _, _, _ in
                self?.cleanupPagExportSession()
            }
            if let popover = activity.popoverPresentationController {
                if exportButton.window != nil {
                    popover.sourceView = exportButton
                    popover.sourceRect = exportButton.bounds
                } else {
                    popover.sourceView = view
                    popover.sourceRect = CGRect(x: view.bounds.midX, y: view.bounds.midY, width: 1, height: 1)
                    popover.permittedArrowDirections = []
                }
            }
            present(activity, animated: true)
        }
    #endif

    private func finishPagExportCancelled(progressVC _: UIViewController) {
        dismiss(animated: true) { [weak self] in
            self?.cleanupPagExportSession()
        }
    }

    private func finishPagExportFailed(message: String, progressVC _: UIViewController) {
        dismiss(animated: true) { [weak self] in
            self?.cleanupPagExportSession()
            self?.presentExportAlert(title: "Export Failed", message: message)
        }
    }

    func cleanupPagExportSession() {
        pagExportSession?.cleanup()
        pagExportSession = nil
        documentPickerPurpose = .saveAs
        updateExportButtonState()
        UIMenuSystem.main.setNeedsRevalidate()
    }
}
