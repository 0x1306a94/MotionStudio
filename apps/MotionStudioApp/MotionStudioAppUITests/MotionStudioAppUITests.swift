//
//  MotionStudioAppUITests.swift
//  MotionStudioAppUITests
//
//  End-to-end smoke test: document creation, editor window (canvas render
//  path), shape creation command, and undo wiring.
//

import XCTest

final class MotionStudioAppUITests: XCTestCase {

    override func setUpWithError() throws {
        continueAfterFailure = false
    }

    @MainActor
    func testNewDocumentAddShapeAndUndo() throws {
        let app = XCUIApplication()
        app.launch()

        // Create a new document; this opens the editor and instantiates the
        // Metal canvas (ms_canvas_create + first frame draw).
        app.typeKey("n", modifierFlags: .command)

        let addButton = app.buttons["Add Rectangle"]
        XCTAssertTrue(addButton.waitForExistence(timeout: 15),
                      "editor window did not appear")
        addButton.click()

        let row = app.staticTexts["Rectangle 1"]
        XCTAssertTrue(row.waitForExistence(timeout: 5), "shape layer was not added")

        // Undo removes the layer (core command stack + system UndoManager).
        app.typeKey("z", modifierFlags: .command)
        XCTAssertTrue(row.waitForNonExistence(timeout: 5), "undo did not remove the layer")
    }
}
