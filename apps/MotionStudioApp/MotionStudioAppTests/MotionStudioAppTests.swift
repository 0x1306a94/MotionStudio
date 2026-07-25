//
//  MotionStudioAppTests.swift
//  MotionStudioAppTests
//

import Foundation
@testable import MotionStudio
import Testing
import UIKit

struct MotionStudioAppTests {
    @Test @MainActor
    func `save copy preserves source document`() throws {
        let temporaryDirectoryURL = FileManager.default.temporaryDirectory
            .appendingPathComponent("MotionStudioAppTests-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: temporaryDirectoryURL, withIntermediateDirectories: true)
        defer {
            try? FileManager.default.removeItem(at: temporaryDirectoryURL)
        }

        let sourceURL = temporaryDirectoryURL.appendingPathComponent("Source.motionproject")
        let copyURL = temporaryDirectoryURL.appendingPathComponent("Copy.motionproject")
        let document = MotionProjectDocument(fileURL: sourceURL)
        document.markEdited()

        let fileWrapper = try document.packageFileWrapper()
        try fileWrapper.write(to: copyURL, options: .atomic, originalContentsURL: nil)

        #expect(document.saveURL == sourceURL)
        #expect(document.hasUnsavedChanges)
        #expect(try Data(contentsOf: copyURL.appendingPathComponent("document.json")) == document.snapshotData())
        #expect(FileManager.default.fileExists(atPath: copyURL.appendingPathComponent("assets").path))
    }
}
