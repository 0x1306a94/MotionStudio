//
//  SceneDelegate.swift
//  MotionStudio
//
//  Created by king on 2026/7/23.
//

import UIKit

class SceneDelegate: UIResponder, UIWindowSceneDelegate {
    private enum WindowRole {
        case launcher
        case editor
    }

    var window: UIWindow?
    private var securityScopedProjectURL: URL?
    private var openingDocument: MotionProjectDocument?

    /// Project URL currently shown or loading in this editor scene, if any.
    var openProjectURL: URL? {
        if let editor = window?.rootViewController as? EditorViewController {
            return editor.document.saveURL
        }
        return openingDocument?.saveURL ?? securityScopedProjectURL
    }

    func scene(_ scene: UIScene, willConnectTo _: UISceneSession, options connectionOptions: UIScene.ConnectionOptions) {
        guard let windowScene = scene as? UIWindowScene else {
            return
        }

        #if targetEnvironment(macCatalyst)
            if let titlebar = windowScene.titlebar {
                titlebar.titleVisibility = .hidden
                titlebar.toolbar = nil
            }
        #endif

        let request = editorRequest(from: connectionOptions)
        if case let .openProject(url)? = request,
           MotionStudioEditorRouter.discardDuplicateEditorSessionIfNeeded(windowScene.session, projectURL: url)
        {
            return
        }

        let window = UIWindow(windowScene: windowScene)
        self.window = window

        let role: WindowRole
        if let request {
            role = .editor
            configureEditorWindow(window, request: request)
            NotificationCenter.default.post(name: .motionStudioEditorSceneDidConnect, object: windowScene.session)
        } else {
            role = .launcher
            configureLauncherWindow(window)
        }

        configureWindowSizeRestrictions(for: windowScene, role: role)
        configureInitialWindowSize(for: windowScene, role: role)
        window.makeKeyAndVisible()
    }

    func scene(_: UIScene, openURLContexts URLContexts: Set<UIOpenURLContext>) {
        MotionStudioEditorRouter.openProjectURLs(URLContexts.map(\.url))
    }

    private func editorRequest(from connectionOptions: UIScene.ConnectionOptions) -> MotionStudioSceneActivity.Request? {
        if let request = MotionStudioSceneActivity.request(from: connectionOptions.userActivities.first) {
            return request
        }

        guard let url = connectionOptions.urlContexts
            .map(\.url)
            .first(where: MotionStudioEditorRouter.isMotionProjectURL)
        else {
            return nil
        }
        return .openProject(url)
    }

    private func configureLauncherWindow(_ window: UIWindow) {
        let projectList = ProjectListViewController()
        window.rootViewController = UINavigationController(rootViewController: projectList)
    }

    private func configureEditorWindow(_ window: UIWindow, request: MotionStudioSceneActivity.Request) {
        switch request {
        case .newProject:
            createEditorDocument(in: window)
        case let .openProject(url):
            openEditorDocument(at: url, in: window)
        }
    }

    private func showEditor(document: MotionProjectDocument, in window: UIWindow) {
        window.rootViewController = EditorViewController(document: document)
    }

    private func createEditorDocument(in window: UIWindow) {
        do {
            let url = try ProjectLibraryStore.makeNewProjectURL()
            let loadingViewController = EditorSceneLoadingViewController(title: url.deletingPathExtension().lastPathComponent)
            window.rootViewController = loadingViewController

            let document = MotionProjectDocument(fileURL: url)
            openingDocument = document
            document.save(to: url, for: .forCreating) { [weak self] success in
                Task { @MainActor in
                    self?.finishCreatingDocument(at: url, success: success)
                }
            }
        } catch {
            let loadingViewController = EditorSceneLoadingViewController(title: "New Motion Project")
            window.rootViewController = loadingViewController
            loadingViewController.showOpenError(error)
        }
    }

    private func finishCreatingDocument(at url: URL, success: Bool) {
        guard let document = openingDocument else { return }
        openingDocument = nil
        guard let window else { return }

        if success {
            document.markSaved(to: url)
            ProjectLibraryStore.remember(url: url)
            showEditor(document: document, in: window)
        } else {
            (window.rootViewController as? EditorSceneLoadingViewController)?.showOpenError(CocoaError(.fileWriteUnknown))
        }
    }

    private func openEditorDocument(at url: URL, in window: UIWindow) {
        let loadingViewController = EditorSceneLoadingViewController(title: url.deletingPathExtension().lastPathComponent)
        window.rootViewController = loadingViewController

        let shouldStopAccessing = url.startAccessingSecurityScopedResource()
        if shouldStopAccessing {
            securityScopedProjectURL = url
        }

        let document = MotionProjectDocument(fileURL: url)
        openingDocument = document
        document.open { [weak self] success in
            Task { @MainActor in
                self?.finishOpeningDocument(at: url, success: success, shouldStopAccessing: shouldStopAccessing)
            }
        }
    }

    private func finishOpeningDocument(at url: URL, success: Bool, shouldStopAccessing: Bool) {
        guard let document = openingDocument else { return }
        openingDocument = nil
        guard let window else { return }

        if success {
            document.markSaved(to: url)
            ProjectLibraryStore.remember(url: url)
            showEditor(document: document, in: window)
        } else {
            (window.rootViewController as? EditorSceneLoadingViewController)?.showOpenError(CocoaError(.fileReadCorruptFile))
            if shouldStopAccessing {
                url.stopAccessingSecurityScopedResource()
                securityScopedProjectURL = nil
            }
        }
    }

    private func minimumWindowSize(for role: WindowRole) -> CGSize {
        #if targetEnvironment(macCatalyst)
            switch role {
            case .launcher:
                return CGSize(width: 820, height: 560)
            case .editor:
                return CGSize(width: 1180, height: 760)
            }
        #else
            switch role {
            case .launcher:
                return CGSize(width: 640, height: 480)
            case .editor:
                return CGSize(width: 900, height: 640)
            }
        #endif
    }

    private func configureWindowSizeRestrictions(for windowScene: UIWindowScene, role: WindowRole) {
        windowScene.sizeRestrictions?.minimumSize = minimumWindowSize(for: role)
        #if targetEnvironment(macCatalyst)
            switch role {
            case .launcher:
                windowScene.sizeRestrictions?.maximumSize = minimumWindowSize(for: role)
            case .editor:
                windowScene.sizeRestrictions?.maximumSize = CGSize(width: CGFloat.greatestFiniteMagnitude,
                                                                   height: CGFloat.greatestFiniteMagnitude)
            }
        #endif
    }

    private func configureInitialWindowSize(for windowScene: UIWindowScene, role: WindowRole) {
        #if targetEnvironment(macCatalyst)
            let frame = initialWindowFrame(for: windowScene, role: role)
            let preferences = UIWindowScene.GeometryPreferences.Mac(systemFrame: frame)
            windowScene.requestGeometryUpdate(preferences) { error in
                NSLog("MotionStudio: failed to set initial window frame: %@", error.localizedDescription)
            }
        #endif
    }

    #if targetEnvironment(macCatalyst)
        private func preferredWindowSize(for role: WindowRole) -> CGSize {
            switch role {
            case .launcher:
                CGSize(width: 820, height: 560)
            case .editor:
                CGSize(width: 1440, height: 920)
            }
        }

        private func initialWindowFrame(for windowScene: UIWindowScene, role: WindowRole) -> CGRect {
            let preferredSize = preferredWindowSize(for: role)
            let minimumSize = minimumWindowSize(for: role)
            let screenFrame = windowScene.screen.bounds
            let maximumSize = CGSize(width: screenFrame.width * 0.9,
                                     height: screenFrame.height * 0.9)
            let size = CGSize(width: min(preferredSize.width, max(minimumSize.width, maximumSize.width)),
                              height: min(preferredSize.height, max(minimumSize.height, maximumSize.height)))
            let origin = CGPoint(x: screenFrame.midX - size.width * 0.5,
                                 y: screenFrame.midY - size.height * 0.5)
            return CGRect(origin: origin, size: size)
        }
    #endif

    func sceneDidDisconnect(_: UIScene) {
        (window?.rootViewController as? EditorViewController)?.saveBeforeSceneDisconnect()
        if let securityScopedProjectURL {
            securityScopedProjectURL.stopAccessingSecurityScopedResource()
            self.securityScopedProjectURL = nil
        }
    }
}

private final class EditorSceneLoadingViewController: UIViewController {
    private let projectTitle: String
    private let statusLabel = UILabel()

    init(title: String) {
        projectTitle = title
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemBackground

        statusLabel.translatesAutoresizingMaskIntoConstraints = false
        statusLabel.font = .preferredFont(forTextStyle: .headline)
        statusLabel.textColor = .secondaryLabel
        statusLabel.text = "Opening \(projectTitle)..."
        view.addSubview(statusLabel)

        NSLayoutConstraint.activate([
            statusLabel.centerXAnchor.constraint(equalTo: view.centerXAnchor),
            statusLabel.centerYAnchor.constraint(equalTo: view.centerYAnchor),
        ])
    }

    func showOpenError(_ error: Error) {
        statusLabel.text = "Open failed"
        let alert = UIAlertController(title: "Open Failed",
                                      message: error.localizedDescription,
                                      preferredStyle: .alert)
        alert.addAction(UIAlertAction(title: "OK", style: .default))
        present(alert, animated: true)
    }
}
