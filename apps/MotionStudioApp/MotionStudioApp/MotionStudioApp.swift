//
//  MotionStudioApp.swift
//  MotionStudioApp
//
//  Document-based app entry: one editor window per document.
//

//import SwiftUI
//
//@main
//struct MotionStudioApp: App {
//    var body: some Scene {
//        DocumentGroup(newDocument: { MotionProjectState() }) { file in
//            EditorRootView(document: file.document, fileURL: file.fileURL)
//            #if targetEnvironment(macCatalyst)
//                .background(WindowSizeConfigurator())
//            #endif
//        }
//        #if targetEnvironment(macCatalyst)
//        .defaultSize(
//            width: WindowSizeConfiguration.defaultSize.width,
//            height: WindowSizeConfiguration.defaultSize.height,
//        )
//        #endif
//        .commands {
//            MotionStudioDocumentCommands()
//        }
//    }
//}
