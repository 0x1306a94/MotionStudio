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
            perform: { [weak self] name, edit in
                self?.perform(name, edit: edit)
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
            projectPanel.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            projectPanel.topAnchor.constraint(equalTo: topToolbar.bottomAnchor),
            projectPanel.bottomAnchor.constraint(equalTo: timelinePanel.topAnchor),
            projectPanel.widthAnchor.constraint(equalToConstant: Metrics.projectPanelWidth),

            inspectorPanel.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            inspectorPanel.topAnchor.constraint(equalTo: topToolbar.bottomAnchor),
            inspectorPanel.bottomAnchor.constraint(equalTo: timelinePanel.topAnchor),
            inspectorPanel.widthAnchor.constraint(equalToConstant: Metrics.inspectorPanelWidth),
        ])
    }

    func configureSidePanelContainer(_ panel: UIView) {
        panel.translatesAutoresizingMaskIntoConstraints = false
        panel.backgroundColor = Palette.panelBackground
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

    /// Unobscured canvas area insets from the current panel/toolbar/timeline frames.
    func currentCanvasViewportInsets() -> UIEdgeInsets {
        var insets = UIEdgeInsets.zero
        insets.left = 20
        insets.right = 20
        insets.top = 20
        insets.bottom = Metrics.creationToolbarSpacing + Metrics.creationToolbarHeight + 20
        return insets
    }
}
