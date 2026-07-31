//
//  AppDelegate.swift
//  MotionStudio
//
//  Created by king on 2026/7/23.
//

import UIKit

@main
class AppDelegate: UIResponder, UIApplicationDelegate {
    func application(_: UIApplication, didFinishLaunchingWithOptions _: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {
        // Override point for customization after application launch.
        #if DEBUG
            print("Application directory: \(NSHomeDirectory())")
        #endif
        return true
    }

    override func buildMenu(with builder: UIMenuBuilder) {
        super.buildMenu(with: builder)
        guard builder.system == UIMenuSystem.main else { return }

        let saveCommand = UIKeyCommand(title: "Save",
                                       image: nil,
                                       action: #selector(EditorViewController.saveCurrentDocument),
                                       input: "s",
                                       modifierFlags: .command)
        let saveAsCommand = UIKeyCommand(title: "Save As...",
                                         image: nil,
                                         action: #selector(EditorViewController.saveDocumentAs),
                                         input: "s",
                                         modifierFlags: [.command, .shift])
        // Not ⌘E: that conflicts with system Edit → Use Selection for Find.
        let exportMP4Command = UIKeyCommand(title: "Export MP4...",
                                            image: nil,
                                            action: #selector(EditorViewController.exportMP4),
                                            input: "e",
                                            modifierFlags: [.command, .alternate])
        let saveMenu = UIMenu(title: "",
                              options: .displayInline,
                              children: [saveCommand, saveAsCommand, exportMP4Command])
        builder.insertSibling(saveMenu, beforeMenu: .close)

        let closeCommand = UIKeyCommand(title: "Close",
                                        image: nil,
                                        action: #selector(EditorViewController.requestCloseWindow),
                                        input: "w",
                                        modifierFlags: .command)
        builder.replaceChildren(ofMenu: .close) { _ in
            [closeCommand]
        }

        let bringToFront = UIKeyCommand(title: "Bring to Front",
                                        image: nil,
                                        action: #selector(EditorViewController.bringLayersToFront),
                                        input: "]",
                                        modifierFlags: [.command, .alternate])
        let bringForward = UIKeyCommand(title: "Bring Forward",
                                        image: nil,
                                        action: #selector(EditorViewController.bringLayersForward),
                                        input: "]",
                                        modifierFlags: .command)
        let sendBackward = UIKeyCommand(title: "Send Backward",
                                        image: nil,
                                        action: #selector(EditorViewController.sendLayersBackward),
                                        input: "[",
                                        modifierFlags: .command)
        let sendToBack = UIKeyCommand(title: "Send to Back",
                                      image: nil,
                                      action: #selector(EditorViewController.sendLayersToBack),
                                      input: "[",
                                      modifierFlags: [.command, .alternate])
        let arrangeMenu = UIMenu(title: "Arrange",
                                 children: [bringToFront, bringForward, sendBackward, sendToBack])
        builder.insertSibling(arrangeMenu, afterMenu: .edit)
    }

    // MARK: UISceneSession Lifecycle

    func application(_: UIApplication, configurationForConnecting connectingSceneSession: UISceneSession, options _: UIScene.ConnectionOptions) -> UISceneConfiguration {
        let configuration = UISceneConfiguration(name: "Default Configuration", sessionRole: connectingSceneSession.role)
        configuration.delegateClass = SceneDelegate.self
        return configuration
    }

    func application(_: UIApplication, didDiscardSceneSessions _: Set<UISceneSession>) {
        // Called when the user discards a scene session.
        // If any sessions were discarded while the application was not running, this will be called shortly after application:didFinishLaunchingWithOptions.
        // Use this method to release any resources that were specific to the discarded scenes, as they will not return.
    }

    // MARK: Finder / external document open

    func application(_: UIApplication, open url: URL, options _: [UIApplication.OpenURLOptionsKey: Any] = [:]) -> Bool {
        MotionStudioEditorRouter.openProjectURLs([url])
        return true
    }
}
