//
//  MotionStudioAppApp.swift
//  MotionStudioApp
//
//  Document-based app entry: one editor window per document.
//

import SwiftUI

@main
struct MotionStudioAppApp: App {
    var body: some Scene {
        DocumentGroup(newDocument: { MotionDocument() }) { file in
            EditorRootView(document: file.document)
        }
    }
}
