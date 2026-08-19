//
//  SvgImportCoordinator.swift
//  MotionStudioApp
//
//  Presents a document picker and imports a selected SVG into the current composition.
//

import UIKit
import UniformTypeIdentifiers

@MainActor
final class SvgImportCoordinator: NSObject {
    private weak var presenter: UIViewController?
    private let document: MotionProjectDocument
    // perform is NSObject reserve
    private let performUndo: (String, () -> Void) -> Void
    var onImported: ((SvgImportOutcome) -> Void)?
    var onFailed: ((String) -> Void)?

    init(presenter: UIViewController,
         document: MotionProjectDocument,
         perform: @escaping (String, () -> Void) -> Void)
    {
        self.presenter = presenter
        self.document = document
        performUndo = perform
    }

    func presentImport() {
        let types: [UTType] = {
            if let svg = UTType(filenameExtension: "svg") {
                return [svg]
            }
            return [.xml]
        }()
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: types, asCopy: true)
        picker.delegate = self
        picker.allowsMultipleSelection = false
        presenter?.present(picker, animated: true)
    }

    private func importSvg(from url: URL) {
        let data: Data
        do {
            data = try Data(contentsOf: url)
        } catch {
            onFailed?(error.localizedDescription)
            return
        }
        document.syncProjectRoot()
        let compositionID = document.core.firstCompositionID
        let stem = url.deletingPathExtension().lastPathComponent
        let rootName = stem.isEmpty ? nil : stem
        do {
            let outcome = try document.core.importSvg(compositionID: compositionID, data: data, rootName: rootName)
            performUndo("Import SVG") {
                self.onImported?(outcome)
            }
        } catch let SvgImportError.failed(message) {
            onFailed?(message)
        } catch {
            onFailed?(error.localizedDescription)
        }
    }
}

extension SvgImportCoordinator: UIDocumentPickerDelegate {
    func documentPicker(_: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
        guard let url = urls.first else {
            return
        }
        let accessed = url.startAccessingSecurityScopedResource()
        defer {
            if accessed {
                url.stopAccessingSecurityScopedResource()
            }
        }
        importSvg(from: url)
    }
}
