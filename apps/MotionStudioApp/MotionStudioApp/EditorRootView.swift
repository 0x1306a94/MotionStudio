//
//  EditorRootView.swift
//  MotionStudioApp
//
//  Editor workspace layout. Mac Catalyst keeps the desktop-style three-column
//  arrangement; iPadOS keeps the canvas primary and opens side tools as panels.
//

import SwiftUI

private let handleHeight: CGFloat = 6

struct EditorRootView: View {
    let document: MotionDocument
    let fileURL: URL?

    @State private var editorState = EditorState()
    @State private var timelineHeight: CGFloat = 220
    @State private var exportDocument = MotionDocumentExport()
    @State private var isSaveAsPresented = false
    @State private var saveError: MotionDocumentSaveError?
    @State private var commandRegistrationID = UUID()
    @State private var currentFileURL: URL?
    @State private var savedRevision: Int?
    @Environment(\.undoManager) private var undoManager
    @Environment(\.scenePhase) private var scenePhase

    @State private var showProject = false
    @State private var showInspector = false

    var body: some View {
        GeometryReader { proxy in
            let minHeight: CGFloat = 140
            let maxHeight = max(minHeight, ceil(proxy.size.height * 0.35))
            let clampedTimeline = min(max(timelineHeight, minHeight), maxHeight)
            let topHeight = max(0, proxy.size.height - handleHeight - clampedTimeline)
            ZStack(alignment: .topLeading) {
                VStack(alignment: .leading, spacing: 0) {
                    topWorkspace(height: topHeight)
                        .frame(maxWidth: .infinity)
                    TimelineResizeHandle(height: $timelineHeight,
                                         minHeight: minHeight,
                                         maxHeight: maxHeight)
                    TimelineView(document: document,
                                 editorState: editorState,
                                 perform: perform,
                                 registerEdit: registerEdit,
                                 clearSelection: clearSelection)
                        .frame(maxWidth: .infinity)
                        .frame(height: clampedTimeline)
                }
                .zIndex(0)

                floatingPanelOverlay(size: proxy.size)
                    .frame(width: proxy.size.width, height: proxy.size.height)
                    .zIndex(1)
            }
        }
        .ignoresSafeArea(.keyboard, edges: .bottom)
        .toolbar {
            #if !targetEnvironment(macCatalyst)
                ToolbarItem(placement: .topBarLeading) {
                    Button {
                        showInspector = false
                        showProject.toggle()
                    } label: {
                        Label("Project", systemImage: "folder")
                    }
                    .accessibilityLabel("Toggle Project Panel")
                }
            #endif
            ToolbarItemGroup(placement: .topBarTrailing) {
                #if !targetEnvironment(macCatalyst)
                    layerCreationMenu
                    saveButton
                #endif
                #if !targetEnvironment(macCatalyst)
                    Button {
                        showProject = false
                        showInspector.toggle()
                    } label: {
                        Label("Inspector", systemImage: "slider.horizontal.3")
                    }
                    .accessibilityLabel("Toggle Inspector Panel")
                #endif
                UnsavedChangesIndicator(isVisible: hasUnsavedChanges)
            }
        }
        .onAppear {
            initializeSaveStateIfNeeded()
            registerDocumentCommands()
        }
        .onDisappear {
            MotionDocumentCommandRegistry.shared.unregister(id: commandRegistrationID)
        }
        .onChange(of: document.core.revision) { _, _ in
            registerDocumentCommands()
        }
        .onChange(of: editorState.selectedLayerID) { _, _ in
            registerDocumentCommands()
        }
        .onChange(of: fileURL) { _, newURL in
            currentFileURL = newURL
            markSaved()
        }
        .onChange(of: scenePhase) { _, phase in
            if phase == .active {
                registerDocumentCommands()
            }
        }
        .fileExporter(isPresented: $isSaveAsPresented,
                      document: exportDocument,
                      contentType: .motionProjectDocument,
                      defaultFilename: defaultExportFilename)
        { result in
            switch result {
            case let .success(url):
                currentFileURL = url
                markSaved()
            case let .failure(error):
                saveError = MotionDocumentSaveError(message: error.localizedDescription)
            }
        }
        .alert("Save Failed", isPresented: saveErrorIsPresented) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(saveError?.message ?? "The document could not be saved.")
        }
    }

    private var documentRevision: Int {
        document.core.revision
    }

    private var hasUnsavedChanges: Bool {
        currentFileURL == nil || savedRevision != documentRevision
    }

    private var canSaveDocument: Bool {
        hasUnsavedChanges
    }

    private var defaultExportFilename: String {
        currentFileURL?.deletingPathExtension().lastPathComponent ?? "Untitled"
    }

    private var saveErrorIsPresented: Binding<Bool> {
        Binding {
            saveError != nil
        } set: { isPresented in
            if !isPresented {
                saveError = nil
            }
        }
    }

    private var layerCreationMenu: some View {
        Menu {
            Button {
                addRectangleLayer()
            } label: {
                Label("Rectangle", systemImage: "rectangle")
            }

            Button {
                addEllipseLayer()
            } label: {
                Label("Ellipse", systemImage: "circle")
            }
        } label: {
            Label("Add Layer", systemImage: "plus")
        }
        .accessibilityLabel("Add Layer")
    }

    private var saveButton: some View {
        Button {
            saveDocument()
        } label: {
            Label("Save", systemImage: "square.and.arrow.down")
        }
        .disabled(!canSaveDocument)
        .accessibilityLabel("Save")
    }

    @ViewBuilder
    private func topWorkspace(height: CGFloat) -> some View {
        #if targetEnvironment(macCatalyst)
            HStack(alignment: .top, spacing: 0) {
                ProjectPanelView(document: document, clearSelection: clearSelection)
                    .frame(width: 220, height: height)
                Divider().frame(height: height)
                CanvasContainer(document: document,
                                editorState: editorState,
                                clearSelection: clearSelection)
                    .frame(maxWidth: .infinity)
                    .frame(height: height)
                Divider().frame(height: height)
                InspectorView(document: document, editorState: editorState, perform: perform)
                    .frame(width: 280, height: height)
            }
        #else
            CanvasContainer(document: document,
                            editorState: editorState,
                            clearSelection: clearSelection)
                .frame(maxWidth: .infinity)
                .frame(height: height)
        #endif
    }

    @ViewBuilder
    private func floatingPanelOverlay(size: CGSize) -> some View {
        #if !targetEnvironment(macCatalyst)
            if showProject || showInspector {
                ZStack(alignment: showProject ? .leading : .trailing) {
                    Color.black.opacity(0.18)
                        .onTapGesture {
                            showProject = false
                            showInspector = false
                        }

                    if showProject {
                        FloatingEditorPanel(title: "Project",
                                            systemImage: "folder",
                                            dismiss: { showProject = false })
                        {
                            ProjectPanelView(document: document, clearSelection: clearSelection)
                        }
                        .frame(width: floatingPanelWidth(for: size, preferred: 300), height: size.height)
                        .transition(.move(edge: .leading).combined(with: .opacity))
                    }

                    if showInspector {
                        FloatingEditorPanel(title: "Inspector",
                                            systemImage: "slider.horizontal.3",
                                            dismiss: { showInspector = false })
                        {
                            InspectorView(document: document,
                                          editorState: editorState,
                                          perform: perform)
                        }
                        .frame(width: floatingPanelWidth(for: size, preferred: 340), height: size.height)
                        .transition(.move(edge: .trailing).combined(with: .opacity))
                    }
                }
                .frame(width: size.width, height: size.height)
                .animation(.easeInOut(duration: 0.18), value: showProject)
                .animation(.easeInOut(duration: 0.18), value: showInspector)
            }
        #endif
    }

    private func floatingPanelWidth(for size: CGSize, preferred: CGFloat) -> CGFloat {
        min(preferred, max(260, floor(size.width * 0.82)))
    }

    // MARK: - Saving

    private func initializeSaveStateIfNeeded() {
        guard savedRevision == nil else { return }
        currentFileURL = fileURL
        savedRevision = documentRevision
    }

    private func registerDocumentCommands() {
        MotionDocumentCommandRegistry.shared.register(
            id: commandRegistrationID,
            handlers: MotionDocumentCommandHandlers(canSave: canSaveDocument,
                                                    canDeleteLayer: editorState.selectedLayerID != nil,
                                                    save: saveDocument,
                                                    saveAs: prepareSaveAs,
                                                    addRectangle: addRectangleLayer,
                                                    addEllipse: addEllipseLayer,
                                                    deleteLayer: deleteSelectedLayer),
        )
    }

    private func saveDocument() {
        guard let currentFileURL else {
            prepareSaveAs()
            return
        }

        do {
            let data = try document.snapshot(contentType: .motionProjectDocument)
            try data.write(to: currentFileURL, options: .atomic)
            markSaved()
        } catch {
            saveError = MotionDocumentSaveError(message: error.localizedDescription)
        }
    }

    private func prepareSaveAs() {
        do {
            exportDocument = try MotionDocumentExport(data: document.snapshot(contentType: .motionProjectDocument))
            isSaveAsPresented = true
        } catch {
            saveError = MotionDocumentSaveError(message: error.localizedDescription)
        }
    }

    private func addRectangleLayer() {
        let compositionID = document.core.firstCompositionID
        perform("Add Rectangle") {
            editorState.selectedLayerID = document.core.addRectLayer(compositionID: compositionID)
        }
    }

    private func addEllipseLayer() {
        let compositionID = document.core.firstCompositionID
        perform("Add Ellipse") {
            editorState.selectedLayerID = document.core.addEllipseLayer(compositionID: compositionID)
        }
    }

    private func deleteSelectedLayer() {
        guard let selected = editorState.selectedLayerID else { return }
        let compositionID = document.core.firstCompositionID
        perform("Delete Layer") {
            document.core.removeLayer(compositionID: compositionID, layerID: selected)
        }
        editorState.selectedLayerID = nil
    }

    private func clearSelection() {
        editorState.selectedLayerID = nil
        editorState.selectedTimelineProperty = nil
        editorState.selectedTimelineSegment = nil
    }

    private func markSaved() {
        savedRevision = documentRevision
        registerDocumentCommands()
    }

    // MARK: - Undo integration

    //
    // The core keeps its own undo stack; the system UndoManager mirrors it so
    // ⌘Z / three-finger swipe and the document's "unsaved changes" tracking
    // work. Each edit registers one action; the action's closure performs the
    // core undo/redo and registers the inverse, keeping both stacks in sync.

    /// Runs an edit command and registers it with the system UndoManager.
    private func perform(_ actionName: String, edit: () -> Void) {
        edit()
        registerEdit(actionName)
    }

    /// Registers an already-executed edit with the system UndoManager.
    /// Call on drag end (the core merged the drag into one command).
    private func registerEdit(_ actionName: String) {
        guard let undoManager else { return }
        undoManager.setActionName(actionName)
        registerInverse(redo: false, undoManager: undoManager)
    }

    private func registerInverse(redo: Bool, undoManager: UndoManager) {
        undoManager.registerUndo(withTarget: document.core) { core in
            if redo {
                core.performRedo()
            } else {
                core.performUndo()
            }
            registerInverse(redo: !redo, undoManager: undoManager)
        }
    }
}

private struct FloatingEditorPanel<Content: View>: View {
    let title: LocalizedStringKey
    let systemImage: String
    let dismiss: () -> Void
    @ViewBuilder let content: () -> Content

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 8) {
                Label(title, systemImage: systemImage)
                    .font(.headline)
                Spacer()
                Button(action: dismiss) {
                    Image(systemName: "xmark")
                }
                .buttonStyle(.borderless)
                .accessibilityLabel("Close")
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 8)

            Divider()

            content()
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .background(.regularMaterial)
        .overlay(alignment: .trailing) {
            Rectangle()
                .fill(.separator)
                .frame(width: 1)
        }
        .shadow(color: .black.opacity(0.22), radius: 18, x: 0, y: 8)
    }
}

private struct UnsavedChangesIndicator: View {
    let isVisible: Bool

    var body: some View {
        if isVisible {
            HStack(spacing: 6) {
                Circle()
                    .fill(.orange)
                    .frame(width: 7, height: 7)
                Text("Unsaved")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .padding(.leading, 10)
            .padding(.trailing, 10)
            .accessibilityLabel("Unsaved changes")
        }
    }
}

/// Drag handle that resizes the timeline panel, clamped to [min, max].
private struct TimelineResizeHandle: View {
    @Binding var height: CGFloat
    let minHeight: CGFloat
    let maxHeight: CGFloat
    @GestureState private var startHeight: CGFloat?
    @State private var isHovering = false

    private var isDragging: Bool {
        startHeight != nil
    }

    private var isActive: Bool {
        isHovering || isDragging
    }

    var body: some View {
        ZStack {
            Rectangle()
                .fill(isActive ? Color.accentColor.opacity(0.18) : Color.secondary.opacity(0.15))
            Rectangle().fill(.separator).frame(height: 1)
            // Grabber pill: the visual affordance that the strip is a draggable
            // resize handle. Brightens on hover/drag so the interaction reads.
            Capsule()
                .fill(isActive ? Color.accentColor : Color.secondary.opacity(0.5))
                .frame(width: 36, height: 3)
        }
        .frame(height: handleHeight)
        .contentShape(Rectangle())
        .onHover { isHovering = $0 }
        .gesture(
            DragGesture(minimumDistance: 0)
                .updating($startHeight) { _, state, _ in
                    if state == nil {
                        state = height
                    }
                }
                .onChanged { value in
                    guard let start = startHeight else { return }
                    // Dragging up (negative translation) grows the panel.
                    let next = start - value.translation.height
                    height = min(max(next, minHeight), maxHeight)
                },
        )
    }
}

/// Wires the canvas to the document with a stable composition ID.
private struct CanvasContainer: View {
    let document: MotionDocument
    let editorState: EditorState
    let clearSelection: () -> Void

    var body: some View {
        let core = document.core
        // Subscribe to the document revision so the canvas redraws on any model
        // mutation (edits, keyframes, visibility), not only on playhead moves.
        let revision = core.revision
        let compositionID = core.firstCompositionID
        CanvasView(core: core,
                   compositionID: compositionID,
                   playheadFrame: editorState.playheadFrame,
                   isPlaying: editorState.isPlaying,
                   duration: core.duration(compositionID: compositionID),
                   frameRate: core.frameRate(compositionID: compositionID),
                   previewBackdrop: editorState.previewBackdrop,
                   revision: revision)
        { frame in
            editorState.playheadFrame = frame
        }
        .contentShape(Rectangle())
        .onTapGesture(perform: clearSelection)
    }
}
