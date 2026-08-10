//
//  PagExportSettingsViewController.swift
//  MotionStudioApp
//
//  Settings sheet for PAG export (vector-first; optional bitmap / video sequence).
//

import MotionStudioBridging
import UIKit

struct PagExportSettings {
    var allowBitmapExport: Bool
    var bmpSequenceType: MS_PAG_BMP_SEQUENCE_TYPE
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
    private let sequenceLabel = UILabel()
    private let sequenceControl = UISegmentedControl(items: ["Auto", "Video", "Bitmap"])
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

        fallbackLabel.text = "Allow bitmap export (_bmp)"
        fallbackLabel.font = .preferredFont(forTextStyle: .body)
        fallbackLabel.translatesAutoresizingMaskIntoConstraints = false

        fallbackSwitch.isOn = false
        fallbackSwitch.translatesAutoresizingMaskIntoConstraints = false
        fallbackSwitch.addTarget(self, action: #selector(fallbackChanged), for: .valueChanged)

        let fallbackRow = UIStackView(arrangedSubviews: [fallbackLabel, fallbackSwitch])
        fallbackRow.axis = .horizontal
        fallbackRow.alignment = .center
        fallbackRow.distribution = .equalSpacing

        sequenceLabel.text = "_bmp sequence type"
        sequenceLabel.font = .preferredFont(forTextStyle: .body)
        sequenceLabel.translatesAutoresizingMaskIntoConstraints = false

        sequenceControl.selectedSegmentIndex = 0
        sequenceControl.isEnabled = false
        sequenceControl.translatesAutoresizingMaskIntoConstraints = false

        var exportConfig = UIButton.Configuration.filled()
        exportConfig.title = "Export"
        exportConfig.cornerStyle = .large
        exportButton.configuration = exportConfig
        exportButton.addTarget(self, action: #selector(exportTapped), for: .touchUpInside)
        exportButton.translatesAutoresizingMaskIntoConstraints = false

        let stack = UIStackView(arrangedSubviews: [
            summaryLabel, fallbackRow, sequenceLabel, sequenceControl, exportButton,
        ])
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

    @objc private func fallbackChanged() {
        sequenceControl.isEnabled = fallbackSwitch.isOn
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
        let sequenceType: MS_PAG_BMP_SEQUENCE_TYPE = switch sequenceControl.selectedSegmentIndex {
        case 1:
            .VIDEO
        case 2:
            .BITMAP
        default:
            .AUTO
        }
        onExport?(PagExportSettings(allowBitmapExport: fallbackSwitch.isOn,
                                    bmpSequenceType: sequenceType))
    }
}
