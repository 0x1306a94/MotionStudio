//
//  ProjectPanelShaderReferenceTests.swift
//  MotionStudioAppTests
//

import Foundation
@testable import MotionStudio
import MotionStudioBridging
import Testing

@MainActor
struct ProjectPanelShaderReferenceTests {
    @Test
    func `shader references include layer names using the shader`() {
        let document = MotionProjectState()
        let core = document.core
        let compositionID = core.firstCompositionID
        let shaderID = core.addShader(name: "Ripple")
        let firstLayerID = core.addRectLayer(compositionID: compositionID)
        let secondLayerID = core.addEllipseLayer(compositionID: compositionID)
        let unusedLayerID = core.addPathLayer(compositionID: compositionID)

        core.setLayerName(firstLayerID, name: "Hero Fill")
        core.setLayerName(secondLayerID, name: "Accent Stroke")
        core.setLayerName(unusedLayerID, name: "Unused Path")
        core.addStrokeStyle(layerID: secondLayerID)
        core.setStylePaintMode(layerID: firstLayerID, index: 0, mode: .SHADER, shaderID: shaderID)
        core.setStylePaintMode(layerID: secondLayerID, index: 1, mode: .SHADER, shaderID: shaderID)

        let references = core.shaderReferences(shaderID)

        #expect(references.map(\.layerName) == ["Hero Fill", "Accent Stroke"])
    }

    @Test
    func `asset references include image layer names using the asset`() throws {
        let temporaryDirectoryURL = FileManager.default.temporaryDirectory
            .appendingPathComponent("ProjectPanelShaderReferenceTests-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: temporaryDirectoryURL, withIntermediateDirectories: true)
        defer {
            try? FileManager.default.removeItem(at: temporaryDirectoryURL)
        }

        let sourceURL = temporaryDirectoryURL.appendingPathComponent("source.png")
        try Self.writePNG(to: sourceURL)

        let document = MotionProjectState()
        let core = document.core
        core.setProjectRoot(temporaryDirectoryURL.path(percentEncoded: false))
        let compositionID = core.firstCompositionID
        let assetID = core.importImageAsset(sourceURL: sourceURL, preferredFileName: "photo.png", width: 1, height: 1)
        let firstLayerID = core.addImageLayer(compositionID: compositionID)
        let secondLayerID = core.addImageLayer(compositionID: compositionID)
        let unusedLayerID = core.addImageLayer(compositionID: compositionID)

        core.setLayerName(firstLayerID, name: "Hero Image")
        core.setLayerName(secondLayerID, name: "Background Image")
        core.setLayerName(unusedLayerID, name: "Unused Image")
        core.setImageAsset(layerID: firstLayerID, assetID: assetID)
        core.setImageAsset(layerID: secondLayerID, assetID: assetID)

        let references = core.assetReferences(assetID)

        #expect(references.map(\.layerName) == ["Hero Image", "Background Image"])
    }

    private static func writePNG(to url: URL) throws {
        let png = Data([
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
            0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00, 0x00, 0x90,
            0x77, 0x53, 0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0xF8,
            0xCF, 0xC0, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0xFE, 0x02, 0xFE, 0x00, 0x00,
            0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
        ])
        try png.write(to: url)
    }
}
