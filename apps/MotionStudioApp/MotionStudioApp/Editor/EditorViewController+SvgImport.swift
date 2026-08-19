//
//  EditorViewController+SvgImport.swift
//  MotionStudioApp
//
//  File → Import SVG… : document picker → insert into the current composition.
//

import UIKit

@MainActor
extension EditorViewController {
    @objc func importSVG() {
        guard !isExportInProgress else {
            return
        }
        if document.core.firstCompositionID == 0 {
            presentImportAlert(title: "Import Failed", message: "No composition to import into.")
            return
        }
        if svgImportCoordinator == nil {
            svgImportCoordinator = SvgImportCoordinator(
                presenter: self,
                document: document,
                perform: { [weak self] name, edit in
                    self?.perform(name, edit: edit)
                },
            )
            svgImportCoordinator?.onImported = { [weak self] outcome in
                self?.finishSvgImport(outcome)
            }
            svgImportCoordinator?.onFailed = { [weak self] message in
                self?.presentImportAlert(title: "Import Failed", message: message)
            }
        }
        svgImportCoordinator?.presentImport()
    }

    private func finishSvgImport(_ outcome: SvgImportOutcome) {
        editorState.selectedLayerID = outcome.rootLayerId
        if outcome.diagnostics.isEmpty {
            return
        }
        let lines = outcome.diagnostics.map { diagnostic -> String in
            if diagnostic.nodeName.isEmpty {
                return "\(diagnostic.code): \(diagnostic.message)"
            }
            return "\(diagnostic.code): \(diagnostic.message) (\(diagnostic.nodeName))"
        }
        presentImportAlert(title: "SVG Import Warnings", message: lines.joined(separator: "\n"))
    }

    func presentImportAlert(title: String, message: String) {
        let alert = UIAlertController(title: title, message: message, preferredStyle: .alert)
        alert.addAction(UIAlertAction(title: "OK", style: .default))
        present(alert, animated: true)
    }
}
