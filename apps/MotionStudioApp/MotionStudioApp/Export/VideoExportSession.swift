//
//  VideoExportSession.swift
//  MotionStudioApp
//
//  Temporary output paths and cancel flag for one video export.
//

import Foundation

/// Heap-backed cancel flag so C `cancelFlag` and Swift readers/writers do not
/// share a `withUnsafePointer(to: &storedProperty)` exclusivity region.
final nonisolated class VideoExportCancelState: @unchecked Sendable {
    private let storage = UnsafeMutablePointer<Int32>.allocate(capacity: 1)

    init() {
        storage.initialize(to: 0)
    }

    deinit {
        storage.deinitialize(count: 1)
        storage.deallocate()
    }

    var isCancelled: Bool {
        storage.pointee != 0
    }

    var flagPointer: UnsafePointer<Int32> {
        UnsafePointer(storage)
    }

    func requestCancel() {
        storage.pointee = 1
    }
}

@MainActor
final class VideoExportSession {
    private(set) var temporaryDirectoryURL: URL?
    private(set) var outputURL: URL?
    let cancelState = VideoExportCancelState()

    func prepareOutputURL(projectName: String, fileExtension: String) throws -> URL {
        cleanup()

        let temporaryDirectoryURL = FileManager.default.temporaryDirectory
            .appendingPathComponent("MotionStudioExport-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: temporaryDirectoryURL, withIntermediateDirectories: true)

        let safeName = projectName.isEmpty ? "Untitled" : projectName
        let outputURL = temporaryDirectoryURL
            .appendingPathComponent(safeName)
            .appendingPathExtension(fileExtension)
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
