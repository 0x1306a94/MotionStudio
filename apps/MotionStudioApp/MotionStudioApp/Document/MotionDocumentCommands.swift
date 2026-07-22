//
//  MotionDocumentCommands.swift
//  MotionStudioApp
//
//  Catalyst menu commands for document saving.
//

import Combine
import Foundation
import SwiftUI
import UniformTypeIdentifiers

struct MotionDocumentCommandHandlers {
    let canSave: Bool
    let save: @MainActor () -> Void
    let saveAs: @MainActor () -> Void
}

@MainActor
final class MotionDocumentCommandRegistry: ObservableObject {
    static let shared = MotionDocumentCommandRegistry()

    @Published private(set) var handlers: MotionDocumentCommandHandlers?

    private var activeRegistrationID: UUID?

    private init() {}

    func register(id: UUID, handlers: MotionDocumentCommandHandlers) {
        activeRegistrationID = id
        self.handlers = handlers
    }

    func unregister(id: UUID) {
        guard activeRegistrationID == id else { return }
        activeRegistrationID = nil
        handlers = nil
    }
}

struct MotionDocumentExport: FileDocument {
    static var readableContentTypes: [UTType] {
        [.motionStudioDocument]
    }

    var data: Data

    init(data: Data = Data()) {
        self.data = data
    }

    init(configuration: ReadConfiguration) throws {
        guard let data = configuration.file.regularFileContents else {
            throw CocoaError(.fileReadCorruptFile)
        }
        self.data = data
    }

    func fileWrapper(configuration _: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: data)
    }
}

struct MotionStudioDocumentCommands: Commands {
    @ObservedObject private var registry = MotionDocumentCommandRegistry.shared

    var body: some Commands {
        CommandGroup(replacing: .saveItem) {
            Button("Save") {
                registry.handlers?.save()
            }
            .keyboardShortcut("s", modifiers: .command)
            .disabled(!(registry.handlers?.canSave ?? false))

            Button("Save As...") {
                registry.handlers?.saveAs()
            }
            .keyboardShortcut("s", modifiers: [.command, .shift])
            .disabled(registry.handlers == nil)
        }
    }
}

struct MotionDocumentSaveError: Identifiable {
    let id = UUID()
    let message: String
}
