//
//  ImageImportCoordinator.swift
//  MotionStudioApp
//
//  Presents photo/file pickers and imports selected images into the project package.
//

import ImageIO
import PhotosUI
import UIKit
import UniformTypeIdentifiers

@MainActor
final class ImageImportCoordinator: NSObject {
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
        #if targetEnvironment(macCatalyst)
            presentFilePicker()
        #else
            let sheet = UIAlertController(title: nil, message: nil, preferredStyle: .actionSheet)
            sheet.addAction(UIAlertAction(title: "Photo Library", style: .default) { [weak self] _ in
                self?.presentPhotoPicker()
            })
            sheet.addAction(UIAlertAction(title: "Files", style: .default) { [weak self] _ in
                self?.presentFilePicker()
            })
            sheet.addAction(UIAlertAction(title: "Cancel", style: .cancel))
            if let popover = sheet.popoverPresentationController {
                popover.sourceView = presenter?.view
                popover.sourceRect = CGRect(x: presenter?.view.bounds.midX ?? 0,
                                            y: presenter?.view.bounds.midY ?? 0,
                                            width: 1,
                                            height: 1)
                popover.permittedArrowDirections = []
            }
            presenter?.present(sheet, animated: true)
        #endif
    }

    private func presentPhotoPicker() {
        var configuration = PHPickerConfiguration(photoLibrary: .shared())
        configuration.filter = .images
        configuration.selectionLimit = 1
        let picker = PHPickerViewController(configuration: configuration)
        picker.delegate = self
        presenter?.present(picker, animated: true)
    }

    private func presentFilePicker() {
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: [.image], asCopy: true)
        picker.delegate = self
        picker.allowsMultipleSelection = false
        presenter?.present(picker, animated: true)
    }

    private func importImage(from url: URL) {
        document.syncProjectRoot()
        guard let size = Self.pixelSize(of: url), size.width > 0, size.height > 0 else {
            return
        }
        let preferredName = url.lastPathComponent
        perform("Import Image") {
            _ = document.core.importImageAsset(sourceURL: url,
                                               preferredFileName: preferredName,
                                               width: size.width,
                                               height: size.height)
        }
    }

    static func pixelSize(of url: URL) -> (width: Int, height: Int)? {
        guard let source = CGImageSourceCreateWithURL(url as CFURL, nil),
              let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
              let width = properties[kCGImagePropertyPixelWidth] as? Int,
              let height = properties[kCGImagePropertyPixelHeight] as? Int
        else {
            return nil
        }
        return (width, height)
    }
}

extension ImageImportCoordinator: PHPickerViewControllerDelegate {
    func picker(_ picker: PHPickerViewController, didFinishPicking results: [PHPickerResult]) {
        picker.dismiss(animated: true)
        guard let result = results.first else {
            return
        }
        let typeIdentifier = UTType.image.identifier
        result.itemProvider.loadFileRepresentation(forTypeIdentifier: typeIdentifier) { [weak self] url, _ in
            guard let self, let url else {
                return
            }
            let temporaryURL = FileManager.default.temporaryDirectory
                .appendingPathComponent(UUID().uuidString)
                .appendingPathExtension(url.pathExtension)
            do {
                try FileManager.default.copyItem(at: url, to: temporaryURL)
            } catch {
                return
            }
            Task { @MainActor in
                self.importImage(from: temporaryURL)
                try? FileManager.default.removeItem(at: temporaryURL)
            }
        }
    }
}

extension ImageImportCoordinator: UIDocumentPickerDelegate {
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
        importImage(from: url)
    }
}
