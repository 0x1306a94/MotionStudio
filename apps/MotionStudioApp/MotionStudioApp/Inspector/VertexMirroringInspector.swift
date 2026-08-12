//
//  VertexMirroringInspector.swift
//  MotionStudioApp
//
//  Figma-style three-way vertex mirroring control for the pen tool.
//

import MotionStudioBridging
import SwiftUI

struct VertexMirroringInspector: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    let kind: MS_PATH_EDIT
    let maskIndex: Int
    let vertexId: UInt32
    let path: String
    let isEditable: Bool
    let perform: (String, () -> Void) -> Void

    @Environment(PlayheadClock.self) private var clock

    private var playheadFrame: Int64 {
        clock.frame
    }

    private var degree: Int {
        let _ = core.panelRevision
        return core.networkVertexDegree(entityID: layerID, path: path, frame: playheadFrame,
                                        vertexId: vertexId)
    }

    private var modeBinding: Binding<MS_VERTEX_MIRROR> {
        Binding(
            get: {
                _ = core.panelRevision
                return core.networkVertexMirrorMode(entityID: layerID, path: path,
                                                    frame: playheadFrame, vertexId: vertexId)
            },
            set: { newMode in
                guard isEditable, degree == 2 else {
                    return
                }
                perform("Set Vertex Mirroring") {
                    core.pathEditSetMirrorMode(layerID: layerID, kind: kind, maskIndex: maskIndex,
                                               frame: playheadFrame, vertexId: vertexId,
                                               mode: newMode)
                }
            },
        )
    }

    var body: some View {
        let _ = core.panelRevision
        VStack(alignment: .leading, spacing: 4) {
            Text("Mirroring")
                .font(.caption)
                .foregroundStyle(.secondary)
            Picker("Mirroring", selection: modeBinding) {
                Text("None").tag(MS_VERTEX_MIRROR.NONE)
                Text("Angle").tag(MS_VERTEX_MIRROR.ANGLE)
                Text("Mirror").tag(MS_VERTEX_MIRROR.ANGLE_LENGTH)
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .disabled(!isEditable || degree != 2)
        }
        .font(.callout)
    }
}
