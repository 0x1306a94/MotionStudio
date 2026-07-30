//
//  EditorViewController.swift
//  MotionStudioApp
//
//  UIKit editor shell. Canvas rendering is UIKit/Metal-backed while existing
//  SwiftUI panels are hosted during the UIKit shell migration.
//

import SwiftUI
import UIKit

@MainActor
final class EditorViewController: UIViewController {
    enum Metrics {
        static let timelineMinimumHeight: CGFloat = 140
        static let timelinePreferredHeight: CGFloat = 220
        static let timelineMaximumFraction: CGFloat = 0.38
        static let timelineHandleHeight: CGFloat = 12
        static let timelineCornerRadius: CGFloat = 10
        static let topToolbarContentHeight: CGFloat = 52
        static let creationToolbarHeight: CGFloat = 44
        static let creationToolbarSpacing: CGFloat = 10
        static let creationToolbarCornerRadius: CGFloat = 12
        /// Inset from toolbar edge to tool buttons; selected highlight radius = corner − padding.
        static let creationToolbarPadding: CGFloat = 6
        static let toolButtonCornerRadius: CGFloat = creationToolbarCornerRadius - creationToolbarPadding
        static let sidePanelTopSpacing: CGFloat = 12
        static let sidePanelBottomSpacing: CGFloat = 12
        static let sidePanelHorizontalInset: CGFloat = 16
        static let projectPanelWidth: CGFloat = 260
        static let inspectorPanelWidth: CGFloat = 320
        static let sidePanelCornerRadius: CGFloat = 12
        static let toolbarButtonSize: CGFloat = 36
        static let topToolbarHorizontalInset: CGFloat = 18
        static var topToolbarLeadingInset: CGFloat {
            #if targetEnvironment(macCatalyst)
                return topToolbarHorizontalInset
            #else
                return UIDevice.current.userInterfaceIdiom == .pad ? 70 : topToolbarHorizontalInset
            #endif
        }
    }

    enum Palette {
        static let panelBackground = UIColor.systemBackground
        static let buttonBackground = UIColor.systemBlue.withAlphaComponent(0.12)
        static let buttonTint = UIColor.systemBlue
        static let separator = UIColor.separator.withAlphaComponent(0.35)
    }

    let document: MotionProjectDocument
    let editorState = EditorState()
    let playheadClock = PlayheadClock()
    let editorUndoManager = UndoManager()

    let canvasViewport = UIView()
    let topToolbar = UIVisualEffectView(effect: nil)
    let saveButton = UIButton(type: .system)
    let undoButton = UIButton(type: .system)
    let redoButton = UIButton(type: .system)
    let documentStatusView = UIView()
    let documentDirtyIndicator = UIView()
    let documentStatusLabel = UILabel()
    let projectToggleButton = UIButton(type: .system)
    let inspectorToggleButton = UIButton(type: .system)
    let projectPanel = UIView()
    let inspectorPanel = UIView()
    let creationToolbar = UIView()
    let selectToolButton = UIButton(type: .system)
    let penToolButton = UIButton(type: .system)
    let addRectangleButton = UIButton(type: .system)
    let addEllipseButton = UIButton(type: .system)
    let addImageButton = UIButton(type: .system)
    let imageResizeModeControl = UISegmentedControl(items: ["Container", "Scale"])
    let timelinePanel = UIVisualEffectView(effect: nil)
    let timelineHandle = TimelineGrabberView()

    var canvasViewController: CanvasViewController?
    var projectHostingController: UIHostingController<ProjectPanelView>?
    var inspectorHostingController: UIHostingController<InspectorView>?
    var timelineViewController: TimelineViewController?
    var imageImportCoordinator: ImageImportCoordinator?
    var timelineHeightConstraint: NSLayoutConstraint?

    var timelineHeight = Metrics.timelinePreferredHeight
    var timelineDragStartHeight = Metrics.timelinePreferredHeight
    var isProjectPanelVisible = true
    var isInspectorPanelVisible = true
    var saveAsTemporaryDirectoryURL: URL?

    init(document: MotionProjectDocument) {
        self.document = document
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemBackground
        title = document.isTemporaryDraft ? "Motion Studio" : document.saveURL.deletingPathExtension().lastPathComponent
        configureCanvas()
        configureTopToolbar()
        configureTimeline()
        configureSidePanels()
        configureCreationToolbar()
        observeCreationToolChanges()
        initializeSaveStateIfNeeded()
        becomeFirstResponder()
        #if !targetEnvironment(macCatalyst)
            observeUndoManagerNotifications()
            updateUndoButtonStates()
        #endif
    }

    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear(animated)
        navigationController?.setNavigationBarHidden(true, animated: animated)
    }

    override func viewDidAppear(_ animated: Bool) {
        super.viewDidAppear(animated)
        updateWindowCloseAvailability()
    }

    override func viewDidLayoutSubviews() {
        super.viewDidLayoutSubviews()
        clampTimelineHeight()
    }

    override var canBecomeFirstResponder: Bool {
        true
    }

    override var undoManager: UndoManager? {
        editorUndoManager
    }

    override var keyCommands: [UIKeyCommand]? {
        [
            UIKeyCommand(input: " ", modifierFlags: [], action: #selector(togglePlayback)),
            UIKeyCommand(input: UIKeyCommand.inputEscape, modifierFlags: [], action: #selector(exitPenTool)),
            UIKeyCommand(input: "\u{8}", modifierFlags: [], action: #selector(UIResponderStandardEditActions.delete(_:))),
            UIKeyCommand(input: UIKeyCommand.inputDelete, modifierFlags: [], action: #selector(UIResponderStandardEditActions.delete(_:))),
            UIKeyCommand(input: "s", modifierFlags: [.command], action: #selector(saveCurrentDocument)),
            UIKeyCommand(input: "s", modifierFlags: [.command, .shift], action: #selector(saveDocumentAs)),
            UIKeyCommand(input: "r", modifierFlags: [.command, .shift], action: #selector(addRectangleLayer)),
            UIKeyCommand(input: "e", modifierFlags: [.command, .shift], action: #selector(addEllipseLayer)),
            UIKeyCommand(input: "i", modifierFlags: [.command, .shift], action: #selector(addImageLayer)),
            UIKeyCommand(input: "1", modifierFlags: [.command, .alternate], action: #selector(toggleProjectPanel)),
            UIKeyCommand(input: "2", modifierFlags: [.command, .alternate], action: #selector(toggleInspectorPanel)),
            UIKeyCommand(input: "]", modifierFlags: [.command], action: #selector(bringLayersForward)),
            UIKeyCommand(input: "[", modifierFlags: [.command], action: #selector(sendLayersBackward)),
            UIKeyCommand(input: "]", modifierFlags: [.command, .alternate], action: #selector(bringLayersToFront)),
            UIKeyCommand(input: "[", modifierFlags: [.command, .alternate], action: #selector(sendLayersToBack)),
        ]
    }

    override func canPerformAction(_ action: Selector, withSender sender: Any?) -> Bool {
        switch action {
        case #selector(saveCurrentDocument):
            hasUnsavedChanges
        case #selector(saveDocumentAs),
             #selector(requestCloseWindow),
             #selector(addRectangleLayer),
             #selector(addEllipseLayer),
             #selector(addImageLayer),
             #selector(toggleProjectPanel),
             #selector(toggleInspectorPanel),
             #selector(renameCurrentProject),
             #selector(togglePlayback):
            true
        case #selector(bringLayersToFront):
            canArrangeSelection(.bringToFront)
        case #selector(bringLayersForward):
            canArrangeSelection(.bringForward)
        case #selector(sendLayersBackward):
            canArrangeSelection(.sendBackward)
        case #selector(sendLayersToBack):
            canArrangeSelection(.sendToBack)
        case #selector(UIResponderStandardEditActions.delete(_:)):
            canDeleteSelection()
        default:
            super.canPerformAction(action, withSender: sender)
        }
    }
}
