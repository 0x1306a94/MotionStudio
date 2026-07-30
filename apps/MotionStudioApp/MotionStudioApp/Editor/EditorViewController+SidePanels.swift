//
//  EditorViewController+SidePanels.swift
//  MotionStudioApp
//
//  Floating project and inspector panel management.
//

import SwiftUI
import UIKit

@MainActor
extension EditorViewController {
    func configureSidePanels() {
        configureSidePanelContainer(projectPanel)
        configureSidePanelContainer(inspectorPanel)
        view.addSubview(projectPanel)
        view.addSubview(inspectorPanel)

        let projectHost = UIHostingController(rootView: ProjectPanelView(
            document: document.modelDocument,
            clearSelection: { [weak self] in
                self?.clearSelection()
            },
            importImage: { [weak self] in
                self?.presentImageImport()
            },
            importFont: { [weak self] in
                self?.presentFontImport()
            },
        ))
        embed(projectHost, in: projectPanel)
        projectHostingController = projectHost

        let inspectorHost = UIHostingController(rootView: InspectorView(document: document.modelDocument,
                                                                        editorState: editorState,
                                                                        playheadClock: playheadClock,
                                                                        perform: { [weak self] name, edit in
                                                                            self?.perform(name, edit: edit)
                                                                        }))
        embed(inspectorHost, in: inspectorPanel)
        inspectorHostingController = inspectorHost

        NSLayoutConstraint.activate([
            projectPanel.leadingAnchor.constraint(equalTo: view.leadingAnchor,
                                                  constant: Metrics.sidePanelHorizontalInset),
            projectPanel.topAnchor.constraint(equalTo: topToolbar.bottomAnchor,
                                              constant: Metrics.sidePanelTopSpacing),
            projectPanel.bottomAnchor.constraint(equalTo: timelinePanel.topAnchor,
                                                 constant: -Metrics.sidePanelBottomSpacing),
            projectPanel.widthAnchor.constraint(equalToConstant: Metrics.projectPanelWidth),

            inspectorPanel.trailingAnchor.constraint(equalTo: view.trailingAnchor,
                                                     constant: -Metrics.sidePanelHorizontalInset),
            inspectorPanel.topAnchor.constraint(equalTo: topToolbar.bottomAnchor,
                                                constant: Metrics.sidePanelTopSpacing),
            inspectorPanel.bottomAnchor.constraint(equalTo: timelinePanel.topAnchor,
                                                   constant: -Metrics.sidePanelBottomSpacing),
            inspectorPanel.widthAnchor.constraint(equalToConstant: Metrics.inspectorPanelWidth),
        ])

        updateSidePanelVisibility(animated: false)
    }

    func configureSidePanelContainer(_ panel: UIView) {
        panel.translatesAutoresizingMaskIntoConstraints = false
        panel.backgroundColor = Palette.panelBackground
        panel.layer.cornerRadius = Metrics.sidePanelCornerRadius
        panel.layer.shadowColor = UIColor.black.cgColor
        panel.layer.shadowOpacity = 0.12
        panel.layer.shadowRadius = 14
        panel.layer.shadowOffset = CGSize(width: 0, height: 5)
        panel.clipsToBounds = false
    }

    func embed(_ host: UIHostingController<some View>, in container: UIView) {
        host.view.translatesAutoresizingMaskIntoConstraints = false
        host.view.backgroundColor = .clear
        host.view.clipsToBounds = true
        host.view.layer.cornerRadius = Metrics.sidePanelCornerRadius
        addChild(host)
        container.addSubview(host.view)
        host.didMove(toParent: self)
        NSLayoutConstraint.activate([
            host.view.leadingAnchor.constraint(equalTo: container.leadingAnchor),
            host.view.trailingAnchor.constraint(equalTo: container.trailingAnchor),
            host.view.topAnchor.constraint(equalTo: container.topAnchor),
            host.view.bottomAnchor.constraint(equalTo: container.bottomAnchor),
        ])
    }

    @objc func toggleProjectPanel() {
        isProjectPanelVisible.toggle()
        updateSidePanelVisibility(animated: true)
    }

    @objc func toggleInspectorPanel() {
        isInspectorPanelVisible.toggle()
        updateSidePanelVisibility(animated: true)
    }

    func updateSidePanelVisibility(animated: Bool) {
        let changes = {
            self.projectPanel.alpha = self.isProjectPanelVisible ? 1 : 0
            self.projectPanel.transform = self.isProjectPanelVisible ? .identity : CGAffineTransform(translationX: -18, y: 0)
            self.inspectorPanel.alpha = self.isInspectorPanelVisible ? 1 : 0
            self.inspectorPanel.transform = self.isInspectorPanelVisible ? .identity : CGAffineTransform(translationX: 18, y: 0)
            self.updatePanelToggleButtons()
        }

        projectPanel.isUserInteractionEnabled = isProjectPanelVisible
        inspectorPanel.isUserInteractionEnabled = isInspectorPanelVisible
        if animated {
            UIView.animate(withDuration: 0.18,
                           delay: 0,
                           options: [.beginFromCurrentState, .curveEaseInOut],
                           animations: changes)
        } else {
            changes()
        }
    }

    func updatePanelToggleButtons() {
        updatePanelToggleButton(projectToggleButton, isActive: isProjectPanelVisible)
        updatePanelToggleButton(inspectorToggleButton, isActive: isInspectorPanelVisible)
    }

    /// Unobscured canvas area insets from the current panel/toolbar/timeline frames.
    func currentCanvasViewportInsets() -> UIEdgeInsets {
        var insets = UIEdgeInsets.zero
        if isProjectPanelVisible {
            insets.left = projectPanel.frame.maxX
        }
        if isInspectorPanelVisible {
            insets.right = view.bounds.width - inspectorPanel.frame.minX
        }
        insets.top = topToolbar.frame.maxY
        insets.bottom = view.bounds.height - timelinePanel.frame.minY
        return insets
    }

    func updatePanelToggleButton(_ button: UIButton, isActive: Bool) {
        button.backgroundColor = isActive ? Palette.buttonBackground : .clear
        button.tintColor = isActive ? Palette.buttonTint : .secondaryLabel
    }
}
