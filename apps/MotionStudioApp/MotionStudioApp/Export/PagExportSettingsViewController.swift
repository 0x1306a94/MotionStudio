//
//  PagExportSettingsViewController.swift
//  MotionStudioApp
//
//  Settings sheet for PAG export (vector-first; optional bitmap fallback flag).
//

import UIKit

struct PagExportSettings {
    var allowBitmapFallback: Bool
}

@MainActor
final class PagExportSettingsViewController: UIViewController {
    var onExport: ((PagExportSettings) -> Void)?
    var onCancel: (() -> Void)?

    private let summary: String
    private let durationFrames: Int64
    private let summaryLabel = UILabel()
    private let fallbackLabel = UILabel()
    private let fallbackSwitch = UISwitch()
    private let exportButton = UIButton(type: .system)

    init(summary: String, durationFrames: Int64) {
        self.summary = summary
        self.durationFrames = durationFrames
        super.init(nibName: nil, bundle: nil)
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        nil
    }

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemBackground
        title = "Export PAG"
        navigationItem.rightBarButtonItem = UIBarButtonItem(barButtonSystemItem: .cancel,
                                                            target: self,
                                                            action: #selector(cancelTapped))

        summaryLabel.text = summary
        summaryLabel.numberOfLines = 0
        summaryLabel.textAlignment = .center
        summaryLabel.font = .preferredFont(forTextStyle: .body)
        summaryLabel.textColor = .secondaryLabel
        summaryLabel.translatesAutoresizingMaskIntoConstraints = false

        fallbackLabel.text = "Allow bitmap fallback"
        fallbackLabel.font = .preferredFont(forTextStyle: .body)
        fallbackLabel.translatesAutoresizingMaskIntoConstraints = false

        fallbackSwitch.isOn = false
        fallbackSwitch.translatesAutoresizingMaskIntoConstraints = false

        let fallbackRow = UIStackView(arrangedSubviews: [fallbackLabel, fallbackSwitch])
        fallbackRow.axis = .horizontal
        fallbackRow.alignment = .center
        fallbackRow.distribution = .equalSpacing

        var exportConfig = UIButton.Configuration.filled()
        exportConfig.title = "Export"
        exportConfig.cornerStyle = .large
        exportButton.configuration = exportConfig
        exportButton.addTarget(self, action: #selector(exportTapped), for: .touchUpInside)
        exportButton.translatesAutoresizingMaskIntoConstraints = false

        let stack = UIStackView(arrangedSubviews: [summaryLabel, fallbackRow, exportButton])
        stack.axis = .vertical
        stack.spacing = 20
        stack.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(stack)

        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: view.layoutMarginsGuide.leadingAnchor),
            stack.trailingAnchor.constraint(equalTo: view.layoutMarginsGuide.trailingAnchor),
            stack.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor, constant: 24),
            exportButton.heightAnchor.constraint(equalToConstant: 44),
        ])
    }

    @objc private func cancelTapped() {
        onCancel?()
    }

    @objc private func exportTapped() {
        guard durationFrames > 0 else {
            let alert = UIAlertController(title: "Export Failed",
                                          message: "Composition has no frames to export.",
                                          preferredStyle: .alert)
            alert.addAction(UIAlertAction(title: "OK", style: .default))
            present(alert, animated: true)
            return
        }
        onExport?(PagExportSettings(allowBitmapFallback: fallbackSwitch.isOn))
    }
}
