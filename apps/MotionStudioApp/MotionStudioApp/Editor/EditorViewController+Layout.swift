//
//  EditorViewController+Layout.swift
//  MotionStudioApp
//
//  UIKit layout assembly for the editor shell.
//

import SwiftUI
import UIKit

@MainActor
extension EditorViewController {
    func configureCanvas() {
        canvasViewport.translatesAutoresizingMaskIntoConstraints = false
        canvasViewport.clipsToBounds = false
        view.addSubview(canvasViewport)

        let canvasController = CanvasViewController(document: document.modelDocument, editorState: editorState, clearSelection: { [weak self] in
            self?.clearSelection()
        }, registerEdit: { [weak self] actionName in
            self?.registerEdit(actionName)
        })
        canvasController.viewportInsetsProvider = { [weak self] in
            self?.currentCanvasViewportInsets() ?? .zero
        }
        canvasController.view.translatesAutoresizingMaskIntoConstraints = false
        addChild(canvasController)
        canvasViewport.addSubview(canvasController.view)
        canvasController.didMove(toParent: self)
        canvasViewController = canvasController

        NSLayoutConstraint.activate([
            canvasViewport.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            canvasViewport.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            canvasViewport.topAnchor.constraint(equalTo: view.topAnchor),
            canvasViewport.bottomAnchor.constraint(equalTo: view.bottomAnchor),

            canvasController.view.leadingAnchor.constraint(equalTo: canvasViewport.leadingAnchor),
            canvasController.view.trailingAnchor.constraint(equalTo: canvasViewport.trailingAnchor),
            canvasController.view.topAnchor.constraint(equalTo: canvasViewport.topAnchor),
            canvasController.view.bottomAnchor.constraint(equalTo: canvasViewport.bottomAnchor),
        ])
    }

    func configureTopToolbar() {
        topToolbar.translatesAutoresizingMaskIntoConstraints = false
        topToolbar.backgroundColor = Palette.panelBackground
        topToolbar.contentView.backgroundColor = Palette.panelBackground
        view.addSubview(topToolbar)

        let contentStack = UIStackView()
        contentStack.translatesAutoresizingMaskIntoConstraints = false
        contentStack.axis = .horizontal
        contentStack.alignment = .center
        contentStack.spacing = 8
        contentStack.isLayoutMarginsRelativeArrangement = true
        contentStack.directionalLayoutMargins = NSDirectionalEdgeInsets(top: 0,
                                                                        leading: Metrics.topToolbarLeadingInset,
                                                                        bottom: 0,
                                                                        trailing: Metrics.topToolbarHorizontalInset)
        topToolbar.contentView.addSubview(contentStack)

        configureToolbarButton(saveButton,
                               systemName: "square.and.arrow.down",
                               accessibilityLabel: "Save",
                               action: #selector(saveCurrentDocument))
        configureToolbarButton(projectToggleButton,
                               systemName: "sidebar.left",
                               accessibilityLabel: "Toggle Project Panel",
                               action: #selector(toggleProjectPanel))
        configureToolbarButton(inspectorToggleButton,
                               systemName: "sidebar.right",
                               accessibilityLabel: "Toggle Inspector Panel",
                               action: #selector(toggleInspectorPanel))

        #if !targetEnvironment(macCatalyst)
            configureToolbarButton(undoButton,
                                   systemName: "arrow.uturn.left",
                                   accessibilityLabel: "Undo",
                                   action: #selector(performUndoFromButton))
            configureToolbarButton(redoButton,
                                   systemName: "arrow.uturn.right",
                                   accessibilityLabel: "Redo",
                                   action: #selector(performRedoFromButton))
        #endif

        #if !targetEnvironment(macCatalyst)
            contentStack.addArrangedSubview(saveButton)
        #endif
        contentStack.addArrangedSubview(projectToggleButton)
        #if !targetEnvironment(macCatalyst)
            contentStack.addArrangedSubview(undoButton)
            contentStack.addArrangedSubview(redoButton)
        #endif
        contentStack.addArrangedSubview(UIView())
        contentStack.addArrangedSubview(inspectorToggleButton)

        configureDocumentStatusView()
        topToolbar.contentView.addSubview(documentStatusView)

        let separator = UIView()
        separator.translatesAutoresizingMaskIntoConstraints = false
        separator.backgroundColor = Palette.separator
        topToolbar.contentView.addSubview(separator)

        NSLayoutConstraint.activate([
            topToolbar.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            topToolbar.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            topToolbar.topAnchor.constraint(equalTo: view.topAnchor),
            topToolbar.bottomAnchor.constraint(equalTo: contentStack.bottomAnchor),

            contentStack.leadingAnchor.constraint(equalTo: topToolbar.contentView.leadingAnchor),
            contentStack.trailingAnchor.constraint(equalTo: topToolbar.contentView.trailingAnchor),
            contentStack.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor),
            contentStack.heightAnchor.constraint(equalToConstant: Metrics.topToolbarContentHeight),

            documentStatusView.centerXAnchor.constraint(equalTo: topToolbar.contentView.centerXAnchor),
            documentStatusView.centerYAnchor.constraint(equalTo: contentStack.centerYAnchor),
            documentStatusView.leadingAnchor.constraint(greaterThanOrEqualTo: contentStack.leadingAnchor, constant: 120),
            documentStatusView.trailingAnchor.constraint(lessThanOrEqualTo: contentStack.trailingAnchor, constant: -120),

            separator.leadingAnchor.constraint(equalTo: topToolbar.contentView.leadingAnchor),
            separator.trailingAnchor.constraint(equalTo: topToolbar.contentView.trailingAnchor),
            separator.bottomAnchor.constraint(equalTo: topToolbar.contentView.bottomAnchor),
            separator.heightAnchor.constraint(equalToConstant: 1 / UIScreen.main.scale),
        ])
        updatePanelToggleButtons()
    }

    func configureDocumentStatusView() {
        documentStatusView.translatesAutoresizingMaskIntoConstraints = false
        documentStatusView.isUserInteractionEnabled = true
        documentStatusView.addGestureRecognizer(UITapGestureRecognizer(target: self, action: #selector(renameCurrentProject)))

        documentDirtyIndicator.translatesAutoresizingMaskIntoConstraints = false
        documentDirtyIndicator.backgroundColor = .systemOrange
        documentDirtyIndicator.layer.cornerRadius = 3
        documentDirtyIndicator.isHidden = true

        documentStatusLabel.translatesAutoresizingMaskIntoConstraints = false
        documentStatusLabel.font = .preferredFont(forTextStyle: .headline)
        documentStatusLabel.textColor = .secondaryLabel
        documentStatusLabel.adjustsFontForContentSizeCategory = true
        documentStatusLabel.lineBreakMode = .byTruncatingMiddle

        let statusStack = UIStackView(arrangedSubviews: [documentDirtyIndicator, documentStatusLabel])
        statusStack.translatesAutoresizingMaskIntoConstraints = false
        statusStack.axis = .horizontal
        statusStack.alignment = .center
        statusStack.spacing = 6
        documentStatusView.addSubview(statusStack)

        NSLayoutConstraint.activate([
            documentDirtyIndicator.widthAnchor.constraint(equalToConstant: 6),
            documentDirtyIndicator.heightAnchor.constraint(equalToConstant: 6),

            statusStack.leadingAnchor.constraint(equalTo: documentStatusView.leadingAnchor),
            statusStack.trailingAnchor.constraint(equalTo: documentStatusView.trailingAnchor),
            statusStack.topAnchor.constraint(equalTo: documentStatusView.topAnchor),
            statusStack.bottomAnchor.constraint(equalTo: documentStatusView.bottomAnchor),
        ])
    }

    func configureCreationToolbar() {
        creationToolbar.translatesAutoresizingMaskIntoConstraints = false
        creationToolbar.backgroundColor = Palette.panelBackground
        creationToolbar.layer.cornerRadius = 12
        creationToolbar.layer.shadowColor = UIColor.black.cgColor
        creationToolbar.layer.shadowOpacity = 0.14
        creationToolbar.layer.shadowRadius = 12
        creationToolbar.layer.shadowOffset = CGSize(width: 0, height: 4)
        view.addSubview(creationToolbar)

        let stack = UIStackView(arrangedSubviews: [
            creationButton(systemName: "rectangle", title: "Rectangle", action: #selector(addRectangleLayer)),
            creationButton(systemName: "circle", title: "Ellipse", action: #selector(addEllipseLayer)),
            creationButton(systemName: "photo", title: "Image", action: #selector(addImageLayer)),
        ])
        stack.translatesAutoresizingMaskIntoConstraints = false
        stack.axis = .horizontal
        stack.alignment = .center
        stack.spacing = 6
        stack.isLayoutMarginsRelativeArrangement = true
        stack.directionalLayoutMargins = NSDirectionalEdgeInsets(top: 4, leading: 6, bottom: 4, trailing: 6)
        creationToolbar.addSubview(stack)

        NSLayoutConstraint.activate([
            creationToolbar.centerXAnchor.constraint(equalTo: view.centerXAnchor),
            creationToolbar.bottomAnchor.constraint(equalTo: timelinePanel.topAnchor,
                                                    constant: -Metrics.creationToolbarSpacing),
            creationToolbar.heightAnchor.constraint(equalToConstant: Metrics.creationToolbarHeight),

            stack.leadingAnchor.constraint(equalTo: creationToolbar.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: creationToolbar.trailingAnchor),
            stack.topAnchor.constraint(equalTo: creationToolbar.topAnchor),
            stack.bottomAnchor.constraint(equalTo: creationToolbar.bottomAnchor),
        ])
    }

    func configureTimeline() {
        timelinePanel.translatesAutoresizingMaskIntoConstraints = false
        timelinePanel.backgroundColor = Palette.panelBackground
        timelinePanel.contentView.backgroundColor = Palette.panelBackground
        timelinePanel.clipsToBounds = true
        timelinePanel.layer.cornerRadius = Metrics.timelineCornerRadius
        timelinePanel.layer.maskedCorners = [.layerMinXMinYCorner, .layerMaxXMinYCorner]
        view.addSubview(timelinePanel)

        timelineHandle.translatesAutoresizingMaskIntoConstraints = false
        timelinePanel.contentView.addSubview(timelineHandle)

        let timelineHost = UIHostingController(rootView: UIKitTimelineHostView(document: document.modelDocument,
                                                                               editorState: editorState,
                                                                               perform: { [weak self] name, edit in
                                                                                   self?.perform(name, edit: edit)
                                                                               },
                                                                               registerEdit: { [weak self] name in
                                                                                   self?.registerEdit(name)
                                                                               },
                                                                               clearSelection: { [weak self] in
                                                                                   self?.clearSelection()
                                                                               }))
        timelineHost.view.translatesAutoresizingMaskIntoConstraints = false
        timelineHost.view.backgroundColor = .clear
        addChild(timelineHost)
        timelinePanel.contentView.addSubview(timelineHost.view)
        timelineHost.didMove(toParent: self)
        timelineHostingController = timelineHost

        timelineHeightConstraint = timelinePanel.heightAnchor.constraint(equalToConstant: timelineHeight)
        timelineHeightConstraint?.priority = .required

        NSLayoutConstraint.activate([
            timelinePanel.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            timelinePanel.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            timelinePanel.bottomAnchor.constraint(equalTo: view.bottomAnchor),
            timelineHeightConstraint!,

            timelineHandle.leadingAnchor.constraint(equalTo: timelinePanel.contentView.leadingAnchor),
            timelineHandle.trailingAnchor.constraint(equalTo: timelinePanel.contentView.trailingAnchor),
            timelineHandle.topAnchor.constraint(equalTo: timelinePanel.contentView.topAnchor),
            timelineHandle.heightAnchor.constraint(equalToConstant: Metrics.timelineHandleHeight),

            timelineHost.view.leadingAnchor.constraint(equalTo: timelinePanel.contentView.leadingAnchor),
            timelineHost.view.trailingAnchor.constraint(equalTo: timelinePanel.contentView.trailingAnchor),
            timelineHost.view.topAnchor.constraint(equalTo: timelineHandle.bottomAnchor),
            timelineHost.view.bottomAnchor.constraint(equalTo: timelinePanel.contentView.bottomAnchor),
        ])

        let resize = UIPanGestureRecognizer(target: self, action: #selector(handleTimelineResize(_:)))
        timelineHandle.addGestureRecognizer(resize)
    }

    func configureToolbarButton(_ button: UIButton,
                                systemName: String,
                                accessibilityLabel: String,
                                action: Selector)
    {
        button.translatesAutoresizingMaskIntoConstraints = false
        button.setImage(UIImage(systemName: systemName), for: .normal)
        button.accessibilityLabel = accessibilityLabel
        button.layer.cornerRadius = 8
        button.addTarget(self, action: action, for: .primaryActionTriggered)
        NSLayoutConstraint.activate([
            button.widthAnchor.constraint(equalToConstant: Metrics.toolbarButtonSize),
            button.heightAnchor.constraint(equalToConstant: Metrics.toolbarButtonSize),
        ])
    }

    func creationButton(systemName: String, title: String, action: Selector) -> UIButton {
        var configuration = UIButton.Configuration.filled()
        configuration.image = UIImage(systemName: systemName)
        configuration.title = title
        configuration.imagePadding = 6
        configuration.baseForegroundColor = Palette.buttonTint
        configuration.baseBackgroundColor = Palette.buttonBackground
        configuration.cornerStyle = .medium

        let button = UIButton(configuration: configuration)
        button.translatesAutoresizingMaskIntoConstraints = false
        button.accessibilityLabel = "Add \(title) Layer"
        button.addTarget(self, action: action, for: .primaryActionTriggered)
        return button
    }
}
