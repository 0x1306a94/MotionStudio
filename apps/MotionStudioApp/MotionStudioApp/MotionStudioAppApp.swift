//
//  MotionStudioAppApp.swift
//  MotionStudioApp
//
//  Created by king on 2026/7/21.
//

import SwiftUI

@main
struct MotionStudioAppApp: App {
    var body: some Scene {
        DocumentGroup(newDocument: MotionStudioAppDocument()) { file in
            ContentView(document: file.$document)
        }
    }
}
