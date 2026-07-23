//
//  AppDelegate.swift
//  MotionStudio
//
//  Created by king on 2026/7/23.
//

import UIKit

@main
class AppDelegate: UIResponder, UIApplicationDelegate {
    func application(_ application: UIApplication, didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {
        // Override point for customization after application launch.
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
        let saveMenu = UIMenu(title: "",
                              options: .displayInline,
                              children: [saveCommand, saveAsCommand])
        builder.insertSibling(saveMenu, beforeMenu: .close)

        let closeCommand = UIKeyCommand(title: "Close",
                                        image: nil,
                                        action: #selector(EditorViewController.requestCloseWindow),
                                        input: "w",
                                        modifierFlags: .command)
        builder.replaceChildren(ofMenu: .close) { _ in
            [closeCommand]
        }
    }
    
    // MARK: UISceneSession Lifecycle
    
    func application(_ application: UIApplication, configurationForConnecting connectingSceneSession: UISceneSession, options: UIScene.ConnectionOptions) -> UISceneConfiguration {
        let activity = options.userActivities.first
        let isEditorScene = MotionStudioSceneActivity.request(from: activity) != nil
        let configurationName = isEditorScene ? "Editor Configuration" : "Launcher Configuration"
        let configuration = UISceneConfiguration(name: configurationName, sessionRole: connectingSceneSession.role)
        configuration.delegateClass = SceneDelegate.self
        return configuration
    }
    
    func application(_ application: UIApplication, didDiscardSceneSessions sceneSessions: Set<UISceneSession>) {
        // Called when the user discards a scene session.
        // If any sessions were discarded while the application was not running, this will be called shortly after application:didFinishLaunchingWithOptions.
        // Use this method to release any resources that were specific to the discarded scenes, as they will not return.
    }
}
