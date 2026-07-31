//
//  VideoExportSession.swift
//  MotionStudioApp
//
//  Temporary output paths and cancel flag for one MP4 export.
//

import Foundation

final nonisolated class VideoExportCancelState: @unchecked Sendable {
    nonisolated(unsafe) var flag: Int32 = 0
}

@MainActor
final class VideoExportSession {
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
            .appendingPathExtension("mp4")
        self.temporaryDirectoryURL = temporaryDirectoryURL
        self.outputURL = outputURL
        return outputURL
    }

    func requestCancel() {
        cancelState.flag = 1
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
