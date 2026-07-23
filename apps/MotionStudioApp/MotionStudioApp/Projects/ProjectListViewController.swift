//
//  ProjectListViewController.swift
//  MotionStudioApp
//
//  UIKit start screen that shows available projects before entering the editor.
//

import UIKit
import UniformTypeIdentifiers

private let projectListMainSection = "main"

private struct ProjectListItem {
    let title: String
    let subtitle: String
    let systemImage: String
    let kind: Kind

    enum Kind {
        case newDocument
        case openFile
        case recent(URL?)
    }
}

@MainActor
final class ProjectListViewController: UIViewController {
    private enum Metrics {
        static let contentInset: CGFloat = 28
        static let minimumCardWidth: CGFloat = 220
        static let cardHeight: CGFloat = 168
        static let interItemSpacing: CGFloat = 18
    }

    private var collectionView: UICollectionView!
    private var dataSource: UICollectionViewDiffableDataSource<String, Int>!
    private var items: [ProjectListItem] = []
    private var openingDocument: MotionProjectDocument?

    override func viewDidLoad() {
        super.viewDidLoad()
        view.backgroundColor = .systemBackground
        configureCollectionView()
        configureDataSource()
        applySnapshot()
    }

    override func viewWillAppear(_ animated: Bool) {
        super.viewWillAppear(animated)
        navigationController?.setNavigationBarHidden(true, animated: animated)
    }

    private func configureCollectionView() {
        collectionView = UICollectionView(frame: .zero, collectionViewLayout: makeLayout())
        collectionView.translatesAutoresizingMaskIntoConstraints = false
        collectionView.backgroundColor = .systemBackground
        collectionView.delegate = self
        collectionView.register(ProjectCardCell.self, forCellWithReuseIdentifier: ProjectCardCell.reuseIdentifier)
        view.addSubview(collectionView)

        NSLayoutConstraint.activate([
            collectionView.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            collectionView.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            collectionView.topAnchor.constraint(equalTo: view.topAnchor),
            collectionView.bottomAnchor.constraint(equalTo: view.bottomAnchor),
        ])
    }

    private func makeLayout() -> UICollectionViewLayout {
        UICollectionViewCompositionalLayout { _, environment in
            let availableWidth = environment.container.effectiveContentSize.width - Metrics.contentInset * 2
            let columns = max(1, Int((availableWidth + Metrics.interItemSpacing) / (Metrics.minimumCardWidth + Metrics.interItemSpacing)))
            let itemWidth = max(Metrics.minimumCardWidth,
                                (availableWidth - CGFloat(columns - 1) * Metrics.interItemSpacing) / CGFloat(columns))

            let itemSize = NSCollectionLayoutSize(widthDimension: .absolute(itemWidth),
                                                  heightDimension: .absolute(Metrics.cardHeight))
            let item = NSCollectionLayoutItem(layoutSize: itemSize)

            let groupSize = NSCollectionLayoutSize(widthDimension: .fractionalWidth(1),
                                                   heightDimension: .absolute(Metrics.cardHeight))
            let group = NSCollectionLayoutGroup.horizontal(layoutSize: groupSize, subitems: [item])
            group.interItemSpacing = .fixed(Metrics.interItemSpacing)

            let section = NSCollectionLayoutSection(group: group)
            section.interGroupSpacing = Metrics.interItemSpacing
            section.contentInsets = NSDirectionalEdgeInsets(top: Metrics.contentInset + 72,
                                                            leading: Metrics.contentInset,
                                                            bottom: Metrics.contentInset,
                                                            trailing: Metrics.contentInset)
            section.boundarySupplementaryItems = [self.makeHeaderSupplementaryItem()]
            return section
        }
    }

    private func makeHeaderSupplementaryItem() -> NSCollectionLayoutBoundarySupplementaryItem {
        let size = NSCollectionLayoutSize(widthDimension: .fractionalWidth(1),
                                          heightDimension: .absolute(72))
        return NSCollectionLayoutBoundarySupplementaryItem(layoutSize: size,
                                                           elementKind: ProjectListHeaderView.elementKind,
                                                           alignment: .top)
    }

    private func configureDataSource() {
        collectionView.register(ProjectListHeaderView.self,
                                forSupplementaryViewOfKind: ProjectListHeaderView.elementKind,
                                withReuseIdentifier: ProjectListHeaderView.reuseIdentifier)

        dataSource = UICollectionViewDiffableDataSource<String, Int>(collectionView: collectionView) { [weak self] collectionView, indexPath, itemIndex in
            guard let item = self?.items[itemIndex] else {
                return UICollectionViewCell()
            }
            let cell = collectionView.dequeueReusableCell(withReuseIdentifier: ProjectCardCell.reuseIdentifier,
                                                          for: indexPath) as! ProjectCardCell
            cell.configure(title: item.title,
                           subtitle: item.subtitle,
                           systemImage: item.systemImage,
                           isCreateCard: item.isNewDocument)
            return cell
        }

        dataSource.supplementaryViewProvider = { collectionView, kind, indexPath in
            guard kind == ProjectListHeaderView.elementKind else { return nil }
            let view = collectionView.dequeueReusableSupplementaryView(ofKind: kind,
                                                                       withReuseIdentifier: ProjectListHeaderView.reuseIdentifier,
                                                                       for: indexPath) as! ProjectListHeaderView
            view.configure()
            return view
        }
    }

    private func applySnapshot() {
        items = [
            ProjectListItem(title: "New Motion Project",
                            subtitle: "Create a blank composition",
                            systemImage: "plus.square",
                            kind: .newDocument),
            ProjectListItem(title: "Open Project File",
                            subtitle: "Choose a .motionstudio document",
                            systemImage: "folder",
                            kind: .openFile),
            ProjectListItem(title: "Untitled Motion",
                            subtitle: "Local draft",
                            systemImage: "film.stack",
                            kind: .recent(nil)),
        ]
        var snapshot = NSDiffableDataSourceSnapshot<String, Int>()
        snapshot.appendSections([projectListMainSection])
        snapshot.appendItems(Array(items.indices))
        dataSource.apply(snapshot, animatingDifferences: false)
    }

    private func open(_ item: ProjectListItem) {
        switch item.kind {
        case .newDocument, .recent(nil):
            let document = MotionProjectDocument.makeTemporaryDraft()
            let editor = EditorViewController(document: document)
            navigationController?.pushViewController(editor, animated: true)
        case let .recent(.some(url)):
            openDocument(at: url)
        case .openFile:
            presentDocumentPicker()
        }
    }

    private func presentDocumentPicker() {
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: [.motionProjectDocument], asCopy: false)
        picker.delegate = self
        picker.allowsMultipleSelection = false
        present(picker, animated: true)
    }

    private func openDocument(at url: URL) {
        let document = MotionProjectDocument(fileURL: url)
        openingDocument = document
        document.open { [weak self] success in
            Task { @MainActor in
                self?.finishOpeningDocument(at: url, success: success)
            }
        }
    }

    private func finishOpeningDocument(at url: URL, success: Bool) {
        guard let document = openingDocument else { return }
        openingDocument = nil
        if success {
            document.markSaved(to: url)
            let editor = EditorViewController(document: document)
            navigationController?.pushViewController(editor, animated: true)
        } else {
            presentOpenError(CocoaError(.fileReadCorruptFile))
        }
    }

    private func presentOpenError(_ error: Error) {
        let alert = UIAlertController(title: "Open Failed",
                                      message: error.localizedDescription,
                                      preferredStyle: .alert)
        alert.addAction(UIAlertAction(title: "OK", style: .default))
        present(alert, animated: true)
    }
}

private extension ProjectListItem {
    var isNewDocument: Bool {
        if case .newDocument = kind {
            return true
        }
        return false
    }
}

extension ProjectListViewController: UICollectionViewDelegate {
    func collectionView(_ collectionView: UICollectionView, didSelectItemAt indexPath: IndexPath) {
        guard let itemIndex = dataSource.itemIdentifier(for: indexPath) else { return }
        collectionView.deselectItem(at: indexPath, animated: true)
        open(items[itemIndex])
    }
}

extension ProjectListViewController: UIDocumentPickerDelegate {
    func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
        guard let url = urls.first else { return }
        openDocument(at: url)
    }
}

private final class ProjectListHeaderView: UICollectionReusableView {
    static let elementKind = "ProjectListHeaderView"
    static let reuseIdentifier = "ProjectListHeaderView"

    private let titleLabel = UILabel()
    private let subtitleLabel = UILabel()

    override init(frame: CGRect) {
        super.init(frame: frame)
        configureViews()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        nil
    }

    func configure() {
        titleLabel.text = "MotionStudio"
        subtitleLabel.text = "Choose a project to continue, or start a new motion document."
    }

    private func configureViews() {
        titleLabel.font = .preferredFont(forTextStyle: .largeTitle)
        titleLabel.textColor = .label
        titleLabel.adjustsFontForContentSizeCategory = true

        subtitleLabel.font = .preferredFont(forTextStyle: .body)
        subtitleLabel.textColor = .secondaryLabel
        subtitleLabel.adjustsFontForContentSizeCategory = true

        let stack = UIStackView(arrangedSubviews: [titleLabel, subtitleLabel])
        stack.translatesAutoresizingMaskIntoConstraints = false
        stack.axis = .vertical
        stack.alignment = .leading
        stack.spacing = 6
        addSubview(stack)

        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: leadingAnchor),
            stack.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor),
            stack.centerYAnchor.constraint(equalTo: centerYAnchor),
        ])
    }
}

private final class ProjectCardCell: UICollectionViewCell {
    static let reuseIdentifier = "ProjectCardCell"

    private let iconView = UIImageView()
    private let titleLabel = UILabel()
    private let subtitleLabel = UILabel()

    override init(frame: CGRect) {
        super.init(frame: frame)
        configureViews()
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        nil
    }

    override var isHighlighted: Bool {
        didSet {
            UIView.animate(withDuration: 0.12) {
                self.contentView.transform = self.isHighlighted ? CGAffineTransform(scaleX: 0.98, y: 0.98) : .identity
            }
        }
    }

    func configure(title: String, subtitle: String, systemImage: String, isCreateCard: Bool) {
        titleLabel.text = title
        subtitleLabel.text = subtitle
        iconView.image = UIImage(systemName: systemImage)
        iconView.tintColor = isCreateCard ? .systemBlue : .label
        contentView.layer.borderColor = isCreateCard ? UIColor.systemBlue.withAlphaComponent(0.35).cgColor : UIColor.separator.withAlphaComponent(0.4).cgColor
    }

    private func configureViews() {
        contentView.backgroundColor = .secondarySystemBackground
        contentView.layer.cornerRadius = 10
        contentView.layer.borderWidth = 1
        contentView.layer.borderColor = UIColor.separator.withAlphaComponent(0.4).cgColor

        iconView.translatesAutoresizingMaskIntoConstraints = false
        iconView.contentMode = .scaleAspectFit
        iconView.preferredSymbolConfiguration = UIImage.SymbolConfiguration(pointSize: 30, weight: .medium)

        titleLabel.font = .preferredFont(forTextStyle: .headline)
        titleLabel.textColor = .label
        titleLabel.numberOfLines = 2
        titleLabel.adjustsFontForContentSizeCategory = true

        subtitleLabel.font = .preferredFont(forTextStyle: .subheadline)
        subtitleLabel.textColor = .secondaryLabel
        subtitleLabel.numberOfLines = 2
        subtitleLabel.adjustsFontForContentSizeCategory = true

        let textStack = UIStackView(arrangedSubviews: [titleLabel, subtitleLabel])
        textStack.translatesAutoresizingMaskIntoConstraints = false
        textStack.axis = .vertical
        textStack.spacing = 4

        contentView.addSubview(iconView)
        contentView.addSubview(textStack)

        NSLayoutConstraint.activate([
            iconView.leadingAnchor.constraint(equalTo: contentView.leadingAnchor, constant: 18),
            iconView.topAnchor.constraint(equalTo: contentView.topAnchor, constant: 18),
            iconView.widthAnchor.constraint(equalToConstant: 38),
            iconView.heightAnchor.constraint(equalToConstant: 38),

            textStack.leadingAnchor.constraint(equalTo: contentView.leadingAnchor, constant: 18),
            textStack.trailingAnchor.constraint(equalTo: contentView.trailingAnchor, constant: -18),
            textStack.bottomAnchor.constraint(equalTo: contentView.bottomAnchor, constant: -18),
        ])
    }
}
