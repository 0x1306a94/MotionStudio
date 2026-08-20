//
//  ProjectPanelView.swift
//  MotionStudioApp
//
//  Left panel: project assets, shaders, and the composition list. The layer
//  stack itself lives in the timeline.
//

import MotionStudioBridging
import SwiftUI

struct ProjectPanelView: View {
    let document: MotionProjectState
    let clearSelection: () -> Void
    let importImage: () -> Void
    let perform: (String, () -> Void) -> Void

    @State private var editingShaderID: UInt64?
    @State private var shaderDeleteBlockedReferences: [ProjectPanelShaderReference] = []

    var body: some View {
        let core = document.core
        let _ = core.panelRevision
        let assets = core.assetIDs()
        let shaders = core.shaderIDs()

        VStack(spacing: 0) {
            HStack(spacing: 8) {
                Text("Project")
                    .font(.headline)
                Spacer()
            }
            .padding(8)

            Divider()

            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text("Assets")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Spacer()
                    Button("Import Image", action: importImage)
                        .font(.caption)
                        .buttonStyle(.borderless)
                }

                if assets.isEmpty {
                    ContentUnavailableView("No Assets",
                                           systemImage: "tray",
                                           description: Text("Import images to use them here."))
                        .frame(maxHeight: 140)
                } else {
                    ScrollView {
                        VStack(alignment: .leading, spacing: 2) {
                            ForEach(assets, id: \.self) { assetID in
                                HStack(spacing: 8) {
                                    Image(systemName: "photo")
                                        .frame(width: 16)
                                    VStack(alignment: .leading, spacing: 1) {
                                        Text(core.assetName(assetID))
                                            .lineLimit(1)
                                        Text("\(core.assetWidth(assetID))×\(core.assetHeight(assetID))")
                                            .font(.caption2)
                                            .foregroundStyle(.secondary)
                                    }
                                    Spacer(minLength: 0)
                                }
                                .font(.subheadline)
                                .padding(.horizontal, 8)
                                .padding(.vertical, 6)
                            }
                        }
                    }
                    .frame(maxHeight: 180)
                }
            }
            .padding(8)

            Divider()

            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text("Shaders")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Spacer()
                    Button("New Shader", action: addShader)
                        .font(.caption)
                        .buttonStyle(.borderless)
                }

                if shaders.isEmpty {
                    ContentUnavailableView("No Shaders",
                                           systemImage: "wand.and.stars",
                                           description: Text("Create a process color shader."))
                        .frame(maxHeight: 140)
                } else {
                    ScrollView {
                        VStack(alignment: .leading, spacing: 2) {
                            ForEach(shaders, id: \.self) { shaderID in
                                HStack(spacing: 8) {
                                    Image(systemName: "wand.and.stars")
                                        .frame(width: 16)
                                    Text(core.shaderName(shaderID))
                                        .lineLimit(1)
                                        .id("shader-name-\(shaderID)-\(core.panelRevision)")
                                    Spacer(minLength: 0)
                                }
                                .font(.subheadline)
                                .padding(.horizontal, 8)
                                .padding(.vertical, 6)
                                .contentShape(Rectangle())
                                .contextMenu {
                                    Button("Edit Source") {
                                        editingShaderID = shaderID
                                    }
                                    Button("Delete", role: .destructive) {
                                        deleteShader(shaderID)
                                    }
                                }
                                .onTapGesture {
                                    editingShaderID = shaderID
                                }
                            }
                        }
                        .id("shader-list-\(core.panelRevision)")
                    }
                    .frame(maxHeight: 180)
                }
            }
            .padding(8)

            Divider()

            ScrollView {
                VStack(alignment: .leading, spacing: 6) {
                    Text("Compositions")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .padding(.horizontal, 8)
                        .padding(.top, 8)

                    ForEach(core.compositionIDs(), id: \.self) { id in
                        Button {
                            clearSelection()
                        } label: {
                            HStack(spacing: 8) {
                                Image(systemName: "film")
                                    .frame(width: 16)
                                Text(core.compositionName(id))
                                    .lineLimit(1)
                                Spacer(minLength: 0)
                            }
                            .font(.subheadline)
                            .foregroundStyle(.primary)
                            .padding(.horizontal, 8)
                            .padding(.vertical, 7)
                            .contentShape(Rectangle())
                        }
                        .buttonStyle(.plain)
                        .padding(.horizontal, 6)
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
        }
        .contentShape(Rectangle())
        .onTapGesture(perform: clearSelection)
        .sheet(item: editingShaderItem) { box in
            ShaderEditorSheet(core: core, shaderID: box.id, perform: perform) {
                editingShaderID = nil
            }
        }
        .alert("Cannot Delete Shader", isPresented: shaderDeleteBlockedBinding) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(shaderDeleteBlockedMessage)
        }
    }

    private var editingShaderItem: Binding<ShaderIDBox?> {
        Binding {
            editingShaderID.map(ShaderIDBox.init)
        } set: { newValue in
            editingShaderID = newValue?.id
        }
    }

    private var shaderDeleteBlockedBinding: Binding<Bool> {
        Binding {
            !shaderDeleteBlockedReferences.isEmpty
        } set: { isPresented in
            if !isPresented {
                shaderDeleteBlockedReferences = []
            }
        }
    }

    private var shaderDeleteBlockedMessage: String {
        guard !shaderDeleteBlockedReferences.isEmpty else {
            return "This shader is still referenced by a Fill or Stroke."
        }
        let layerList = shaderDeleteBlockedReferences
            .map { "- \($0.layerName)" }
            .joined(separator: "\n")
        return "This shader is still referenced by these layers:\n\(layerList)"
    }

    private func addShader() {
        let core = document.core
        var newID: UInt64 = 0
        perform("Add Shader") {
            newID = core.addShader(name: uniqueShaderName(core: core))
        }
        if newID != 0 {
            editingShaderID = newID
        }
    }

    private func deleteShader(_ shaderID: UInt64) {
        let references = document.core.shaderReferences(shaderID)
        if !references.isEmpty {
            shaderDeleteBlockedReferences = references
            return
        }
        perform("Remove Shader") {
            _ = document.core.removeShader(shaderID)
        }
    }

    private func uniqueShaderName(core: MotionDocumentCore) -> String {
        let existing = Set(core.shaderIDs().map { core.shaderName($0) })
        if !existing.contains("Shader") {
            return "Shader"
        }
        var index = 2
        while existing.contains("Shader \(index)") {
            index += 1
        }
        return "Shader \(index)"
    }
}

struct ProjectPanelShaderReference: Equatable, Identifiable {
    let layerID: UInt64
    let layerName: String

    var id: UInt64 {
        layerID
    }
}

extension MotionDocumentCore {
    func shaderReferences(_ shaderID: UInt64) -> [ProjectPanelShaderReference] {
        var references: [ProjectPanelShaderReference] = []
        var referencedLayerIDs = Set<UInt64>()

        for compositionID in compositionIDs() {
            for layerID in layerIDs(compositionID: compositionID) {
                guard referencedLayerIDs.contains(layerID) == false else {
                    continue
                }
                if layerReferencesShader(layerID: layerID, shaderID: shaderID) {
                    referencedLayerIDs.insert(layerID)
                    let name = layerName(layerID)
                    references.append(ProjectPanelShaderReference(
                        layerID: layerID,
                        layerName: name.isEmpty ? "Layer \(layerID)" : name,
                    ))
                }
            }
        }

        return references
    }

    private func layerReferencesShader(layerID: UInt64, shaderID: UInt64) -> Bool {
        for index in 0 ..< styleCount(layerID: layerID) {
            if stylePaintMode(layerID: layerID, index: index) == .SHADER,
               styleShaderID(layerID: layerID, index: index) == shaderID
            {
                return true
            }
        }
        return false
    }
}

private struct ShaderIDBox: Identifiable {
    let id: UInt64
}
