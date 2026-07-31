//
//  PagExportSession.swift
//  MotionStudioApp
//
//  Temporary output path for one PAG export.
//

import Foundation

@MainActor
final class PagExportSession {
    private(set) var temporaryDirectoryURL: URL?
    private(set) var outputURL: URL?
    /// Set when the user dismisses progress before encode finishes.
    private(set) var isDiscarded = false

    func prepareOutputURL(projectName: String) throws -> URL {
        cleanup()
        isDiscarded = false

        let temporaryDirectoryURL = FileManager.default.temporaryDirectory
            .appendingPathComponent("MotionStudioExport-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: temporaryDirectoryURL, withIntermediateDirectories: true)

        let safeName = projectName.isEmpty ? "Untitled" : projectName
        let outputURL = temporaryDirectoryURL
            .appendingPathComponent(safeName)
            .appendingPathExtension("pag")
        self.temporaryDirectoryURL = temporaryDirectoryURL
        self.outputURL = outputURL
        return outputURL
    }

    func markDiscarded() {
        isDiscarded = true
    }

    func cleanup() {
        guard let temporaryDirectoryURL else {
            return
        }
        self.temporaryDirectoryURL = nil
        outputURL = nil
        try? FileManager.default.removeItem(at: temporaryDirectoryURL)
    }
}
