//
//  TimelineViewController.swift
//  MotionStudioApp
//
//  UIKit timeline root. Replaces SwiftUI Timeline hosting in the editor shell.
//

import UIKit

@MainActor
final class TimelineViewController: UIViewController {
    private let document: MotionProjectState
    private let editorState: EditorState
    private let playheadClock: PlayheadClock
    private let performEdit: (String, () -> Void) -> Void
    private let registerEdit: (String) -> Void
    private let clearSelection: () -> Void

    private var playheadListenerID: UUID?
    private let placeholderLabel = UILabel()

    init(document: MotionProjectState,
         editorState: EditorState,
         playheadClock: PlayheadClock,
         perform: @escaping (String, () -> Void) -> Void,
         registerEdit: @escaping (String) -> Void,
         clearSelection: @escaping () -> Void)
    {
        self.document = document
        self.editorState = editorState
        self.playheadClock = playheadClock
        performEdit = perform
        self.registerEdit = registerEdit
        self.clearSelection = clearSelection
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .secondarySystemBackground
        placeholderLabel.translatesAutoresizingMaskIntoConstraints = false
        placeholderLabel.numberOfLines = 0
        placeholderLabel.text = "Timeline (UIKit)\nrevision \(document.core.revision)"
        placeholderLabel.textAlignment = .center
        placeholderLabel.textColor = .secondaryLabel
        placeholderLabel.font = .preferredFont(forTextStyle: .title3)
        view.addSubview(placeholderLabel)
        NSLayoutConstraint.activate([
            placeholderLabel.centerXAnchor.constraint(equalTo: view.centerXAnchor),
            placeholderLabel.centerYAnchor.constraint(equalTo: view.centerYAnchor),
            placeholderLabel.leadingAnchor.constraint(greaterThanOrEqualTo: view.leadingAnchor, constant: 16),
            placeholderLabel.trailingAnchor.constraint(lessThanOrEqualTo: view.trailingAnchor, constant: -16),
        ])
        // Keep injected collaborators retained for upcoming tasks.
        _ = editorState
        _ = performEdit
        _ = registerEdit
        _ = clearSelection
    }

    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear(animated)
        if playheadListenerID == nil {
            playheadListenerID = playheadClock.addListener { [weak self] _ in
                self?.handlePlayheadFrameChanged()
            }
        }
    }

    override func viewWillDisappear(_ animated: Bool) {
        super.viewWillDisappear(animated)
        if let playheadListenerID {
            playheadClock.removeListener(playheadListenerID)
            self.playheadListenerID = nil
        }
    }

    private func handlePlayheadFrameChanged() {
        // Task 3: update playhead chrome only.
    }
}
