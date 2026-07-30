//
//  FontImportCoordinator.swift
//  MotionStudioApp
//
//  Presents a file picker and imports selected fonts into the project package.
//

import UIKit
import UniformTypeIdentifiers

@MainActor
final class FontImportCoordinator: NSObject {
    private weak var presenter: UIViewController?
    private let document: MotionProjectDocument
    private let perform: (String, () -> Void) -> Void

    init(presenter: UIViewController,
         document: MotionProjectDocument,
         perform: @escaping (String, () -> Void) -> Void)
    {
        self.presenter = presenter
        self.document = document
        self.perform = perform
    }

    func presentImport() {
        let types: [UTType] = [
            .init(filenameExtension: "ttf") ?? .data,
            .init(filenameExtension: "otf") ?? .data,
            .init(filenameExtension: "ttc") ?? .data,
        ]
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: types, asCopy: true)
        picker.delegate = self
        picker.allowsMultipleSelection = false
        presenter?.present(picker, animated: true)
    }

    private func importFont(from url: URL) {
        document.syncProjectRoot()
        let preferredName = url.lastPathComponent
        perform("Import Font") {
            _ = document.core.importFontAsset(sourceURL: url, preferredFileName: preferredName)
        }
    }
}

extension FontImportCoordinator: UIDocumentPickerDelegate {
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
        importFont(from: url)
    }
}
