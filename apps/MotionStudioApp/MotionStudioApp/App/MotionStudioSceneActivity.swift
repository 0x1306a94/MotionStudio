//
//  MotionStudioSceneActivity.swift
//  MotionStudioApp
//
//  Scene activation payloads for launcher and editor windows.
//

import Foundation

extension Notification.Name {
    static let motionStudioEditorSceneDidConnect = Notification.Name("MotionStudioEditorSceneDidConnect")
}

enum MotionStudioSceneActivity {
    enum Request {
        case newProject
        case openProject(URL)
    }

    static let editorActivityType = "com.0x1306a94.motionstudio.editor"

    private static let requestKindKey = "requestKind"
    private static let projectURLBookmarkKey = "projectURLBookmark"
    private static let projectURLStringKey = "projectURLString"
    private static let newProjectKind = "newProject"
    private static let openProjectKind = "openProject"

    #if targetEnvironment(macCatalyst)
        private static let bookmarkCreationOptions: URL.BookmarkCreationOptions = [.withSecurityScope]
        private static let bookmarkResolutionOptions: URL.BookmarkResolutionOptions = [.withSecurityScope]
    #else
        private static let bookmarkCreationOptions: URL.BookmarkCreationOptions = []
        private static let bookmarkResolutionOptions: URL.BookmarkResolutionOptions = []
    #endif

    static func newProjectActivity() -> NSUserActivity {
        let activity = NSUserActivity(activityType: editorActivityType)
        activity.title = "New Motion Project"
        activity.userInfo = [requestKindKey: newProjectKind]
        return activity
    }

    static func openProjectActivity(url: URL) -> NSUserActivity {
        let activity = NSUserActivity(activityType: editorActivityType)
        activity.title = url.deletingPathExtension().lastPathComponent
        var userInfo: [String: Any] = [
            requestKindKey: openProjectKind,
            projectURLStringKey: url.absoluteString,
        ]
        if let bookmarkData = try? url.bookmarkData(options: bookmarkCreationOptions,
                                                    includingResourceValuesForKeys: nil,
                                                    relativeTo: nil)
        {
            userInfo[projectURLBookmarkKey] = bookmarkData
        }
        activity.userInfo = userInfo
        return activity
    }

    static func request(from activity: NSUserActivity?) -> Request? {
        guard activity?.activityType == editorActivityType,
              let userInfo = activity?.userInfo,
              let kind = userInfo[requestKindKey] as? String
        else {
            return nil
        }

        switch kind {
        case newProjectKind:
            return .newProject
        case openProjectKind:
            guard let url = projectURL(from: userInfo) else { return nil }
            return .openProject(url)
        default:
            return nil
        }
    }

    private static func projectURL(from userInfo: [AnyHashable: Any]) -> URL? {
        if let bookmarkData = userInfo[projectURLBookmarkKey] as? Data {
            var isStale = false
            if let url = try? URL(resolvingBookmarkData: bookmarkData,
                                  options: bookmarkResolutionOptions,
                                  relativeTo: nil,
                                  bookmarkDataIsStale: &isStale), !isStale
            {
                return url
            }
        }

        guard let urlString = userInfo[projectURLStringKey] as? String else { return nil }
        return URL(string: urlString)
    }
}
