//
//  PagExportSession.swift
//  MotionStudioApp
//
//  Temporary output path and cancel flag for one PAG export.
//

import Foundation

@MainActor
final class PagExportSession {
    private(set) var temporaryDirectoryURL: URL?
    private(set) var outputURL: URL?
    let cancelState = VideoExportCancelState()

    func prepareOutputURL(projectName: String) throws -> URL {
        cleanup()

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

    func requestCancel() {
        cancelState.requestCancel()
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
