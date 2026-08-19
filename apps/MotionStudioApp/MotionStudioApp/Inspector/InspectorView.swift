//
//  InspectorView.swift
//  MotionStudioApp
//
//  Property inspector for the selected layer.
//

import Foundation
import SwiftUI
#if canImport(UIKit) && !targetEnvironment(macCatalyst)
    import UIKit
#endif
import MotionStudioBridging

struct InspectorView: View {
    let document: MotionProjectState
    let editorState: EditorState
    let playheadClock: PlayheadClock
    let perform: (String, () -> Void) -> Void

    @State private var keyboardFrame = CGRect.null
    @State private var draftLayerName = ""
    @FocusState private var layerNameFocused: Bool

    var body: some View {
        GeometryReader { proxy in
            let core = document.core
            if let layerID = editorState.selectedLayerID {
                let _ = core.panelRevision
                let isVisible = core.layerIsVisible(layerID)
                let isLocked = core.layerIsLocked(layerID)
                let isEditable = core.layerIsEffectivelyVisible(layerID) && !core.layerIsEffectivelyLocked(layerID)
                let layerName = core.layerName(layerID)
                ScrollView {
                    VStack(alignment: .leading, spacing: 10) {
                        TextField("Layer Name", text: $draftLayerName)
                            .font(.headline)
                            .textFieldStyle(.roundedBorder)
                            .focused($layerNameFocused)
                            .onAppear {
                                draftLayerName = layerName
                            }
                            .onChange(of: layerID) { _, newLayerID in
                                draftLayerName = core.layerName(newLayerID)
                            }
                            .onChange(of: layerName) { _, newValue in
                                if !layerNameFocused {
                                    draftLayerName = newValue
                                }
                            }
                            .onChange(of: layerNameFocused) { _, focused in
                                if focused {
                                    core.beginMergeGroup()
                                } else {
                                    commitLayerName(core: core, layerID: layerID)
                                    core.endMergeGroup()
                                }
                            }
                            .onSubmit {
                                commitLayerName(core: core, layerID: layerID)
                            }
                        LayerEditStatus(isVisible: isVisible, isLocked: isLocked)
                        if core.hasProperty(entityID: layerID, path: shapeSizePath) {
                            Text("Shape")
                                .font(.subheadline)
                                .foregroundStyle(.secondary)
                            ShapeSizeInspector(core: core,
                                               layerID: layerID,
                                               isEditable: isEditable,
                                               perform: perform)
                        }
                        if core.layerType(layerID) == .IMAGE {
                            ImageLayerInspector(core: core,
                                                layerID: layerID,
                                                isEditable: isEditable,
                                                perform: perform)
                        }
                        if core.layerType(layerID) == .TEXT {
                            TextLayerInspector(core: core,
                                               compositionID: core.firstCompositionID,
                                               layerID: layerID,
                                               isEditable: isEditable,
                                               perform: perform)
                        }
                        if core.hasBezierPath(entityID: layerID, path: ShapeProperty.path.path) {
                            PathKeyframeInspector(core: core,
                                                  layerID: layerID,
                                                  isEditable: isEditable,
                                                  perform: perform)
                        }
                        if editorState.tool == .pen,
                           let target = editorState.pathEditTarget,
                           target.layerID == layerID,
                           target.activeVertexId != 0
                        {
                            VertexMirroringInspector(core: core,
                                                     layerID: target.layerID,
                                                     kind: target.kind,
                                                     maskIndex: target.maskIndex,
                                                     vertexId: target.activeVertexId,
                                                     path: target.propertyPath,
                                                     isEditable: isEditable,
                                                     perform: perform)
                        }

                        if core.layerType(layerID) == .GROUP {
                            GroupLayerInspector(core: core,
                                                layerID: layerID,
                                                isEditable: isEditable,
                                                perform: perform)
                        }

                        Text("Transform")
                            .font(.subheadline)
                            .foregroundStyle(.secondary)
                        TransformInspector(core: core,
                                           compositionID: core.firstCompositionID,
                                           layerID: layerID,
                                           pathEditTarget: {
                                               guard editorState.tool == .pen,
                                                     let target = editorState.pathEditTarget,
                                                     target.layerID == layerID
                                               else {
                                                   return nil
                                               }
                                               return target
                                           }(),
                                           isEditable: isEditable,
                                           perform: perform)

                        FollowPathInspector(core: core,
                                            compositionID: core.firstCompositionID,
                                            layerID: layerID,
                                            isEditable: isEditable,
                                            perform: perform)

                        MotionPathInspector(core: core,
                                            compositionID: core.firstCompositionID,
                                            layerID: layerID,
                                            selectedKeyframeIndex: editorState.motionPathSelectedKeyframe,
                                            isEditable: isEditable,
                                            perform: perform,
                                            onSelectKeyframe: { index in
                                                editorState.motionPathLayerID = layerID
                                                editorState.motionPathSelectedKeyframe = index
                                            })

                        if core.layerType(layerID) == .SHAPE || core.layerType(layerID) == .TEXT {
                            FillsInspector(core: core,
                                           layerID: layerID,
                                           isEditable: isEditable,
                                           perform: perform)
                            StrokesInspector(core: core,
                                             layerID: layerID,
                                             isEditable: isEditable,
                                             perform: perform)
                        }

                        MasksInspector(core: core,
                                       layerID: layerID,
                                       isEditable: isEditable,
                                       perform: perform,
                                       onEditMaskPath: { index in
                                           editorState.tool = .pen
                                           editorState.pathEditTarget = .mask(layerID: layerID, maskIndex: index)
                                       })
                        TrackMatteInspector(core: core,
                                            compositionID: core.firstCompositionID,
                                            layerID: layerID,
                                            isEditable: isEditable,
                                            perform: perform)

                        if core.layerType(layerID) == .SHAPE ||
                            core.layerType(layerID) == .IMAGE ||
                            core.layerType(layerID) == .TEXT
                        {
                            EffectsInspector(core: core,
                                             layerID: layerID,
                                             isEditable: isEditable,
                                             perform: perform)
                            LayerStylesInspector(core: core,
                                                 layerID: layerID,
                                                 isEditable: isEditable,
                                                 perform: perform)
                        }
                    }
                    .padding(10)
                    .padding(.bottom, inspectorKeyboardOverlap(keyboardFrame: keyboardFrame, in: proxy))
                }
                .scrollDismissesKeyboard(.interactively)
            } else {
                let compositionID = core.firstCompositionID
                ScrollView {
                    CompositionInspector(core: core,
                                         compositionID: compositionID,
                                         perform: perform)
                        .padding(10)
                        .padding(.bottom, inspectorKeyboardOverlap(keyboardFrame: keyboardFrame,
                                                                   in: proxy))
                }
                .scrollDismissesKeyboard(.interactively)
            }
        }
        .task {
            await observeKeyboardFrames()
        }
        .environment(playheadClock)
    }

    private func observeKeyboardFrames() async {
        #if canImport(UIKit) && !targetEnvironment(macCatalyst)
            for await notification in NotificationCenter.default.notifications(named: UIResponder.keyboardWillChangeFrameNotification) {
                keyboardFrame = inspectorKeyboardEndFrame(from: notification)
            }
        #endif
    }

    private func commitLayerName(core: MotionDocumentCore, layerID: UInt64) {
        let trimmed = draftLayerName.trimmingCharacters(in: .whitespacesAndNewlines)
        let current = core.layerName(layerID)
        if trimmed.isEmpty {
            draftLayerName = current
            return
        }
        guard trimmed != current else {
            draftLayerName = current
            return
        }
        draftLayerName = trimmed
        perform("Rename Layer") {
            core.setLayerName(layerID, name: trimmed)
        }
    }
}
