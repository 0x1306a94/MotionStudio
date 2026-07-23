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
        canvasViewport.backgroundColor = .black
        canvasViewport.clipsToBounds = false
        view.addSubview(canvasViewport)

        let canvasController = CanvasViewController(document: document.modelDocument,
                                                    editorState: editorState,
                                                    clearSelection: clearSelection)
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
        contentStack.directionalLayoutMargins = NSDirectionalEdgeInsets(top: 0, leading: 18, bottom: 0, trailing: 18)
        topToolbar.contentView.addSubview(contentStack)

        configureToolbarButton(closeEditorButton,
                               systemName: "xmark",
                               accessibilityLabel: "Close Editor",
                               action: #selector(closeEditor))
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

        contentStack.addArrangedSubview(closeEditorButton)
        contentStack.addArrangedSubview(saveButton)
        contentStack.addArrangedSubview(projectToggleButton)
        contentStack.addArrangedSubview(UIView())
        contentStack.addArrangedSubview(inspectorToggleButton)

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

            separator.leadingAnchor.constraint(equalTo: topToolbar.contentView.leadingAnchor),
            separator.trailingAnchor.constraint(equalTo: topToolbar.contentView.trailingAnchor),
            separator.bottomAnchor.constraint(equalTo: topToolbar.contentView.bottomAnchor),
            separator.heightAnchor.constraint(equalToConstant: 1 / UIScreen.main.scale),
        ])
        updatePanelToggleButtons()
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
                                                                              perform: perform,
                                                                              registerEdit: registerEdit,
                                                                              clearSelection: clearSelection))
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
