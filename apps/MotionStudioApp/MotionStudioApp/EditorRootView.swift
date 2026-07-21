//
//  EditorRootView.swift
//  MotionStudioApp
//
//  After-Effects-style four-region layout: project panel (left), composition
//  preview (center), inspector (right) and a vertically resizable timeline
//  (bottom). On compact width the side panels move to sheets.
//

import SwiftUI

struct EditorRootView: View {
    let document: MotionDocument

    @State private var editorState = EditorState()
    @State private var timelineHeight: CGFloat = 220
    @Environment(\.undoManager) private var undoManager
    @Environment(\.horizontalSizeClass) private var horizontalSizeClass

    #if os(iOS)
        @State private var showProject = false
        @State private var showInspector = false
    #endif

    var body: some View {
        GeometryReader { proxy in
            let minHeight: CGFloat = 140
            let maxHeight = max(minHeight, proxy.size.height * 0.7)
            VStack(spacing: 0) {
                topColumns
                    .frame(maxHeight: .infinity)
                TimelineResizeHandle(height: $timelineHeight,
                                     minHeight: minHeight,
                                     maxHeight: maxHeight)
                TimelineView(document: document,
                             editorState: editorState,
                             perform: perform,
                             registerEdit: registerEdit)
                    .frame(height: min(max(timelineHeight, minHeight), maxHeight))
            }
        }
        #if os(iOS)
        .toolbar {
            if horizontalSizeClass == .compact {
                ToolbarItem(placement: .topBarLeading) {
                    Button { showProject = true } label: {
                        Image(systemName: "folder")
                    }
                }
                ToolbarItem(placement: .topBarTrailing) {
                    Button { showInspector = true } label: {
                        Image(systemName: "slider.horizontal.3")
                    }
                }
            }
        }
        .sheet(isPresented: $showProject) {
            NavigationStack {
                ProjectPanelView(document: document,
                                 editorState: editorState,
                                 perform: perform)
                    .navigationTitle("Project")
            }
        }
        .sheet(isPresented: $showInspector) {
            NavigationStack {
                InspectorView(document: document,
                              editorState: editorState,
                              perform: perform)
                    .navigationTitle("Inspector")
            }
        }
        #endif
    }

    @ViewBuilder
    private var topColumns: some View {
        #if os(macOS)
            HSplitView {
                ProjectPanelView(document: document, editorState: editorState, perform: perform)
                    .frame(minWidth: 180, idealWidth: 220)
                CanvasContainer(document: document, editorState: editorState)
                    .frame(minWidth: 320, maxWidth: .infinity, maxHeight: .infinity)
                InspectorView(document: document, editorState: editorState, perform: perform)
                    .frame(minWidth: 230, idealWidth: 270)
            }
        #else
            if horizontalSizeClass == .regular {
                HStack(spacing: 0) {
                    ProjectPanelView(document: document, editorState: editorState, perform: perform)
                        .frame(width: 220)
                    Divider()
                    CanvasContainer(document: document, editorState: editorState)
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                    Divider()
                    InspectorView(document: document, editorState: editorState, perform: perform)
                        .frame(width: 280)
                }
            } else {
                CanvasContainer(document: document, editorState: editorState)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        #endif
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
            self.registerInverse(redo: !redo, undoManager: undoManager)
        }
    }
}

/// Drag handle that resizes the timeline panel, clamped to [min, max].
private struct TimelineResizeHandle: View {
    @Binding var height: CGFloat
    let minHeight: CGFloat
    let maxHeight: CGFloat
    @GestureState private var startHeight: CGFloat?

    var body: some View {
        ZStack {
            Rectangle().fill(Color.secondary.opacity(0.15))
            Rectangle().fill(.separator).frame(height: 1)
        }
        .frame(height: 6)
        .contentShape(Rectangle())
        #if os(macOS)
            .onHover { hovering in
                if hovering {
                    NSCursor.resizeUpDown.push()
                } else {
                    NSCursor.pop()
                }
            }
        #endif
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
                    }
            )
    }
}

/// Wires the canvas to the document with a stable composition ID.
private struct CanvasContainer: View {
    let document: MotionDocument
    let editorState: EditorState

    var body: some View {
        let core = document.core
        let compositionID = core.firstCompositionID
        CanvasView(core: core,
                   compositionID: compositionID,
                   playheadFrame: editorState.playheadFrame,
                   isPlaying: editorState.isPlaying,
                   duration: core.duration(compositionID: compositionID),
                   frameRate: core.frameRate(compositionID: compositionID))
        { frame in
            editorState.playheadFrame = frame
        }
    }
}
