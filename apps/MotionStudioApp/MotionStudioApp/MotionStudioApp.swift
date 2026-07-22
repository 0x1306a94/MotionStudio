//
//  MotionStudioApp.swift
//  MotionStudioApp
//
//  Document-based app entry: one editor window per document.
//

import SwiftUI

@main
struct MotionStudioApp: App {
    var body: some Scene {
        DocumentGroup(newDocument: { MotionDocument() }) { file in
            EditorRootView(document: file.document, fileURL: file.fileURL)
        }
        .defaultSize(width: 1280, height: 800)
        .commands {
            MotionStudioDocumentCommands()
        }
    }
}
