//
//  MotionStudioAppApp.swift
//  MotionStudioApp
//
//  Document-based app entry: one editor window per document.
//

import SwiftUI
#if os(macOS)
    import AppKit
#endif

@main
struct MotionStudioAppApp: App {
    var body: some Scene {
        DocumentGroup(newDocument: { MotionDocument() }) { file in
            EditorRootView(document: file.document)
        }
        #if os(macOS)
        .defaultSize(width: Self.defaultWindowSize.width,
                     height: Self.defaultWindowSize.height)
        #endif
    }

    #if os(macOS)
        /// Default editor window = 70% x 80% of the main screen's visible area.
        private static var defaultWindowSize: CGSize {
            let visible = NSScreen.main?.visibleFrame
                ?? NSRect(x: 0, y: 0, width: 1280, height: 800)
            return CGSize(width: ceil(visible.width * 0.7), height: ceil(visible.height * 0.8))
        }
    #endif
}
