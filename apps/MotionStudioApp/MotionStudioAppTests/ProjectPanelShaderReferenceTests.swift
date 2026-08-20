//
//  ProjectPanelShaderReferenceTests.swift
//  MotionStudioAppTests
//

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
}
