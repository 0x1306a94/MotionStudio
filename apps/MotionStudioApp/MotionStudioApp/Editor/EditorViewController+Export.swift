//
//  EditorViewController+Export.swift
//  MotionStudioApp
//
//  File → Export MP4… flow: settings → progress → Files picker.
//

import UIKit

@MainActor
extension EditorViewController {
    @objc func exportMP4() {
        guard videoExportSession == nil else { return }

        let core = document.core
        let compositionID = core.firstCompositionID
        guard compositionID != 0 else {
            presentExportAlert(title: "Export Failed", message: "No composition to export.")
            return
        }

        let size = core.size(compositionID: compositionID)
        let duration = core.duration(compositionID: compositionID)
        let fps = core.frameRate(compositionID: compositionID)
        let exportWidth = VideoExportOptionsBuilder.evenFloor(Int(size.width.rounded()))
        let exportHeight = VideoExportOptionsBuilder.evenFloor(Int(size.height.rounded()))
        let sizeText = if exportWidth != Int(size.width.rounded()) || exportHeight != Int(size.height.rounded()) {
            "\(Int(size.width.rounded()))×\(Int(size.height.rounded())) (export \(exportWidth)×\(exportHeight))"
        } else {
            "\(exportWidth)×\(exportHeight)"
        }
        let fpsText = fps == floor(fps) ? String(Int(fps)) : String(format: "%.2f", fps)
        let summary = "\(sizeText) · \(duration) frames · \(fpsText) fps"

        let settings = VideoExportSettingsViewController(summary: summary, durationFrames: duration)
        settings.onExport = { [weak self] quality in
            self?.dismiss(animated: true) {
                self?.beginVideoExport(quality: quality,
                                       compositionID: compositionID,
                                       size: size,
                                       duration: duration,
                                       frameRate: fps)
            }
        }
        settings.onCancel = { [weak self] in
            self?.dismiss(animated: true)
        }

        let navigation = UINavigationController(rootViewController: settings)
        navigation.modalPresentationStyle = .formSheet
        present(navigation, animated: true)
    }

    func beginVideoExport(quality: VideoExportQuality,
                          compositionID: UInt64,
                          size: CGSize,
                          duration: Int64,
                          frameRate: Double)
    {
        let session = VideoExportSession()
        videoExportSession = session

        let resolved = VideoExportOptionsBuilder.resolve(size: size,
                                                         duration: duration,
                                                         frameRate: frameRate,
                                                         quality: quality)
        let projectName = document.isTemporaryDraft
            ? "Untitled"
            : document.saveURL.deletingPathExtension().lastPathComponent

        let outputURL: URL
        do {
            outputURL = try session.prepareOutputURL(projectName: projectName)
        } catch {
            videoExportSession = nil
            presentExportAlert(title: "Export Failed", message: error.localizedDescription)
            return
        }

        let progressVC = VideoExportProgressViewController()
        progressVC.modalPresentationStyle = .formSheet
        progressVC.onCancel = { [weak session] in
            session?.requestCancel()
        }
        present(progressVC, animated: true)

        let core = document.core
        let cancelState = session.cancelState
        let outputPath = outputURL.path(percentEncoded: false)
        UIMenuSystem.main.setNeedsRevalidate()

        Task.detached(priority: .userInitiated) {
            do {
                try core.exportVideo(compositionID: compositionID,
                                     outputPath: outputPath,
                                     resolved: resolved,
                                     progress: { completed, total in
                                         if cancelState.flag != 0 {
                                             return false
                                         }
                                         Task { @MainActor in
                                             progressVC.update(completed: completed, total: total)
                                         }
                                         return true
                                     },
                                     cancelState: cancelState)
                await MainActor.run {
                    self.finishVideoExportSuccess(outputURL: outputURL, progressVC: progressVC)
                }
            } catch VideoExportError.cancelled {
                await MainActor.run {
                    self.finishVideoExportCancelled(progressVC: progressVC)
                }
            } catch let VideoExportError.failed(message) {
                await MainActor.run {
                    self.finishVideoExportFailed(message: message, progressVC: progressVC)
                }
            } catch {
                await MainActor.run {
                    self.finishVideoExportFailed(message: error.localizedDescription, progressVC: progressVC)
                }
            }
        }
    }

    private func finishVideoExportSuccess(outputURL: URL, progressVC _: UIViewController) {
        dismiss(animated: true) { [weak self] in
            guard let self else { return }
            documentPickerPurpose = .exportMP4
            let picker = UIDocumentPickerViewController(forExporting: [outputURL], asCopy: true)
            picker.delegate = self
            picker.shouldShowFileExtensions = true
            present(picker, animated: true)
        }
    }

    private func finishVideoExportCancelled(progressVC _: UIViewController) {
        dismiss(animated: true) { [weak self] in
            self?.cleanupVideoExportSession()
        }
    }

    private func finishVideoExportFailed(message: String, progressVC _: UIViewController) {
        dismiss(animated: true) { [weak self] in
            self?.cleanupVideoExportSession()
            self?.presentExportAlert(title: "Export Failed", message: message)
        }
    }

    func cleanupVideoExportSession() {
        videoExportSession?.cleanup()
        videoExportSession = nil
        documentPickerPurpose = .saveAs
        UIMenuSystem.main.setNeedsRevalidate()
    }

    private func presentExportAlert(title: String, message: String) {
        let alert = UIAlertController(title: title, message: message, preferredStyle: .alert)
        alert.addAction(UIAlertAction(title: "OK", style: .default))
        present(alert, animated: true)
    }
}
