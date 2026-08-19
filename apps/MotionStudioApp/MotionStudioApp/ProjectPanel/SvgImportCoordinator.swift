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

    func pasteFromClipboard() {
        guard let data = ClipboardSvg.data() else {
            onFailed?("Clipboard does not contain SVG.")
            return
        }
        importSvg(data: data, rootName: nil, undoName: "Paste SVG")
    }

    private func importSvg(from url: URL) {
        let data: Data
        do {
            data = try Data(contentsOf: url)
        } catch {
            onFailed?(error.localizedDescription)
            return
        }
        let stem = url.deletingPathExtension().lastPathComponent
        let rootName = stem.isEmpty ? nil : stem
        importSvg(data: data, rootName: rootName, undoName: "Import SVG")
    }

    private func importSvg(data: Data, rootName: String?, undoName: String) {
        document.syncProjectRoot()
        let compositionID = document.core.firstCompositionID
        do {
            let outcome = try document.core.importSvg(compositionID: compositionID, data: data, rootName: rootName)
            performUndo(undoName) {
                self.onImported?(outcome)
            }
        } catch let SvgImportError.failed(message) {
            onFailed?(message)
        } catch {
            onFailed?(error.localizedDescription)
        }
    }
}

enum ClipboardSvg {
    static var containsSvg: Bool {
        data() != nil
    }

    static func data() -> Data? {
        let board = UIPasteboard.general
        if let svgType = UTType(filenameExtension: "svg"),
           let data = board.data(forPasteboardType: svgType.identifier),
           looksLikeSvg(data)
        {
            return data
        }
        if let data = board.data(forPasteboardType: "public.svg-image"), looksLikeSvg(data) {
            return data
        }
        if let data = board.data(forPasteboardType: "public.xml"), looksLikeSvg(data) {
            return data
        }
        if let string = board.string, let data = string.data(using: .utf8), looksLikeSvg(data) {
            return data
        }
        if let url = board.url, url.pathExtension.lowercased() == "svg" {
            return try? Data(contentsOf: url)
        }
        return nil
    }

    static func looksLikeSvg(_ data: Data) -> Bool {
        guard var text = String(data: data, encoding: .utf8) else {
            return false
        }
        if text.hasPrefix("\u{FEFF}") {
            text.removeFirst()
        }
        let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
        let lower = trimmed.lowercased()
        if lower.hasPrefix("<html") || lower.hasPrefix("<!doctype html") {
            return false
        }
        guard let svgRange = lower.range(of: "<svg") else {
            return false
        }
        let prefix = lower[..<svgRange.lowerBound]
            .trimmingCharacters(in: .whitespacesAndNewlines)
        if prefix.isEmpty {
            return true
        }
        return prefix.hasPrefix("<?xml")
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
