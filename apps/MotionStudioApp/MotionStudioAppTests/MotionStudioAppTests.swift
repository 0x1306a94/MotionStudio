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

    @Test @MainActor
    func `package includes imported image asset files`() throws {
        let temporaryDirectoryURL = FileManager.default.temporaryDirectory
            .appendingPathComponent("MotionStudioAppTests-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: temporaryDirectoryURL, withIntermediateDirectories: true)
        defer {
            try? FileManager.default.removeItem(at: temporaryDirectoryURL)
        }

        let packageURL = temporaryDirectoryURL.appendingPathComponent("WithAsset.motionproject")
        try FileManager.default.createDirectory(at: packageURL, withIntermediateDirectories: true)
        try FileManager.default.createDirectory(at: packageURL.appendingPathComponent("assets"),
                                                withIntermediateDirectories: true)

        let document = MotionProjectDocument(fileURL: packageURL)
        document.syncProjectRoot()

        let sourceURL = temporaryDirectoryURL.appendingPathComponent("red.png")
        // 1x1 PNG
        let png = Data([
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
            0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90,
            0x77, 0x53, 0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0xF8,
            0xCF, 0xC0, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0xFE, 0x02, 0xFE, 0x00, 0x00,
            0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
        ])
        try png.write(to: sourceURL)

        let assetID = document.core.importImageAsset(sourceURL: sourceURL,
                                                     preferredFileName: "red.png",
                                                     width: 1,
                                                     height: 1)
        #expect(assetID != 0)

        let fileWrapper = try document.packageFileWrapper()
        let outURL = temporaryDirectoryURL.appendingPathComponent("Out.motionproject")
        try fileWrapper.write(to: outURL, options: .atomic, originalContentsURL: nil)
        #expect(FileManager.default.fileExists(atPath: outURL.appendingPathComponent("assets/red.png").path))
    }
}
