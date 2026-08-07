//
//  ShaderEditorSheet.swift
//  MotionStudioApp
//
//  Shadertoy-style editor: std140 Inputs (built-ins + user uniforms) above,
//  editable mainImage below. Saves via UpdateShaderDefinitionCommand.
//

import MotionStudioBridging
import SwiftUI

struct ShaderUniformDraft: Identifiable, Equatable {
    var id = UUID()
    var name: String
    var format: MS_UNIFORM_FORMAT
}

struct ShaderEditorSheet: View {
    let core: MotionDocumentCore
    let shaderID: UInt64
    let perform: (String, () -> Void) -> Void
    let onDismiss: () -> Void

    @State private var name: String
    @State private var mainImage: String
    @State private var uniforms: [ShaderUniformDraft]
    @State private var showAddUniform = false
    @State private var editingUniformID: UUID?
    @State private var draftUniformName = ""
    @State private var draftUniformFormat: MS_UNIFORM_FORMAT = .FLOAT

    init(core: MotionDocumentCore, shaderID: UInt64, perform: @escaping (String, () -> Void) -> Void,
         onDismiss: @escaping () -> Void)
    {
        self.core = core
        self.shaderID = shaderID
        self.perform = perform
        self.onDismiss = onDismiss
        _name = State(initialValue: core.shaderName(shaderID))
        _mainImage = State(initialValue: core.shaderMainImage(shaderID))
        var drafts: [ShaderUniformDraft] = []
        let count = core.shaderUniformCount(shaderID)
        for index in 0 ..< count {
            let format = core.shaderUniformFormat(shaderID, index: index)
            drafts.append(ShaderUniformDraft(name: core.shaderUniformName(shaderID, index: index),
                                             format: format == .INVALID ? .FLOAT : format))
        }
        _uniforms = State(initialValue: drafts)
    }

    var body: some View {
        NavigationStack {
            VStack(alignment: .leading, spacing: 12) {
                TextField("Name", text: $name)
                    .textFieldStyle(.roundedBorder)
                    .padding(.top, 20)

                inputsSection
                    .frame(minHeight: 180, maxHeight: 320)
                    .layoutPriority(0)

                Text("mainImage")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                TextEditor(text: $mainImage)
                    .font(.system(.body, design: .monospaced))
                    .scrollContentBackground(.hidden)
                    .padding(8)
                    .frame(minHeight: 280)
                    .layoutPriority(1)
                    .background(Color(uiColor: .secondarySystemBackground), in: RoundedRectangle(cornerRadius: 8))
            }
            .padding()
            .navigationTitle("Edit Shader")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button(action: onDismiss) {
                        Image(systemName: "xmark")
                    }
                    .accessibilityLabel("Cancel")
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button(action: save) {
                        Image(systemName: "checkmark")
                    }
                    .disabled(name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                    .accessibilityLabel("Save")
                }
            }
            .sheet(isPresented: $showAddUniform) {
                uniformEditorSheet(title: "Add Uniform", isNew: true)
            }
            .sheet(item: editingUniformBinding) { draft in
                uniformEditorSheet(title: "Edit Uniform", isNew: false, existingID: draft.id)
            }
        }
        .frame(minWidth: 720, idealWidth: 840, minHeight: 560, idealHeight: 720)
        .presentationDetents([.large])
        .presentationDragIndicator(.visible)
    }

    private var inputsSection: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Inputs")
                .font(.caption)
                .foregroundStyle(.secondary)

            ScrollView {
                VStack(alignment: .leading, spacing: 4) {
                    Text("// Built-ins (injected each frame)")
                        .font(.system(.caption2, design: .monospaced))
                        .foregroundStyle(.secondary)

                    ForEach(Self.builtInRows, id: \.name) { row in
                        Text("\(row.glslType)  \(row.name);  // \(row.comment)")
                            .font(.system(.caption, design: .monospaced))
                            .foregroundStyle(.secondary)
                    }

                    Text("// User uniforms")
                        .font(.system(.caption2, design: .monospaced))
                        .foregroundStyle(.secondary)
                        .padding(.top, 4)

                    ForEach(uniforms) { uniform in
                        HStack(spacing: 8) {
                            Text("\(uniform.format.glslTypeName)  \(uniform.name);")
                                .font(.system(.caption, design: .monospaced))
                            Spacer(minLength: 0)
                            Button("Edit") {
                                draftUniformName = uniform.name
                                draftUniformFormat = uniform.format
                                editingUniformID = uniform.id
                            }
                            .font(.caption)
                            .buttonStyle(.borderless)
                            Button(role: .destructive) {
                                uniforms.removeAll { $0.id == uniform.id }
                            } label: {
                                Image(systemName: "minus.circle")
                            }
                            .buttonStyle(.borderless)
                        }
                    }

                    Button {
                        draftUniformName = ""
                        draftUniformFormat = .FLOAT
                        showAddUniform = true
                    } label: {
                        Label("Add Uniform", systemImage: "plus")
                            .font(.caption)
                    }
                    .buttonStyle(.borderless)
                    .padding(.top, 4)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            .padding(8)
            .background(Color(uiColor: .secondarySystemBackground), in: RoundedRectangle(cornerRadius: 8))
        }
    }

    private var editingUniformBinding: Binding<ShaderUniformDraft?> {
        Binding {
            guard let id = editingUniformID else { return nil }
            return uniforms.first { $0.id == id }
        } set: { newValue in
            if newValue == nil {
                editingUniformID = nil
            }
        }
    }

    private func uniformEditorSheet(title: String, isNew: Bool, existingID: UUID? = nil) -> some View {
        NavigationStack {
            Form {
                TextField("Name", text: $draftUniformName)
                Picker("Format", selection: $draftUniformFormat) {
                    ForEach(MS_UNIFORM_FORMAT.editableCases) { format in
                        Text(format.glslTypeName).tag(format)
                    }
                }
            }
            .navigationTitle(title)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button {
                        showAddUniform = false
                        editingUniformID = nil
                    } label: {
                        Image(systemName: "xmark")
                    }
                    .accessibilityLabel("Cancel")
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button {
                        applyUniformDraft(isNew: isNew, existingID: existingID)
                    } label: {
                        Image(systemName: "checkmark")
                    }
                    .disabled(!isValidUniformName(draftUniformName.trimmingCharacters(in: .whitespacesAndNewlines),
                                                  excluding: existingID))
                    .accessibilityLabel("Done")
                }
            }
        }
        .presentationDetents([.medium])
    }

    private func applyUniformDraft(isNew: Bool, existingID: UUID?) {
        let trimmed = draftUniformName.trimmingCharacters(in: .whitespacesAndNewlines)
        guard isValidUniformName(trimmed, excluding: existingID) else { return }
        if isNew {
            uniforms.append(ShaderUniformDraft(name: trimmed, format: draftUniformFormat))
            showAddUniform = false
        } else if let existingID,
                  let index = uniforms.firstIndex(where: { $0.id == existingID })
        {
            uniforms[index].name = trimmed
            uniforms[index].format = draftUniformFormat
            editingUniformID = nil
        }
    }

    private func isValidUniformName(_ name: String, excluding: UUID?) -> Bool {
        guard !name.isEmpty else { return false }
        if Self.builtInNames.contains(name) { return false }
        return !uniforms.contains { $0.name == name && $0.id != excluding }
    }

    private func save() {
        let trimmedName = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedName.isEmpty,
              let json = Self.encodeUniformsJSON(uniforms)
        else {
            return
        }
        perform("Update Shader") {
            _ = core.updateShader(id: shaderID, name: trimmedName, mainImage: mainImage, uniformsJSON: json)
        }
        onDismiss()
    }

    private static func encodeUniformsJSON(_ uniforms: [ShaderUniformDraft]) -> String? {
        let objects: [[String: Any]] = uniforms.map { uniform in
            [
                "name": uniform.name,
                "format": uniform.format.jsonFormatName,
                "count": 1,
            ]
        }
        guard let data = try? JSONSerialization.data(withJSONObject: objects, options: []),
              let text = String(data: data, encoding: .utf8)
        else {
            return nil
        }
        return text
    }

    private static let builtInNames: Set<String> = [
        "iResolution", "iTime", "iTimeDelta", "iFrame", "iFrameRate",
    ]

    private static let builtInRows: [(name: String, glslType: String, comment: String)] = [
        ("iResolution", "vec3", "xy = source bounds px, z = 1"),
        ("iTime", "float", "seconds"),
        ("iTimeDelta", "float", "seconds since previous draw"),
        ("iFrame", "int", "frame index"),
        ("iFrameRate", "float", "composition fps"),
    ]
}

extension MS_UNIFORM_FORMAT {
    static var editableCases: [MS_UNIFORM_FORMAT] {
        [.FLOAT, .FLOAT2, .FLOAT3, .FLOAT4]
    }

    var glslTypeName: String {
        switch self {
        case .FLOAT:
            "float"
        case .FLOAT2:
            "vec2"
        case .FLOAT3:
            "vec3"
        case .FLOAT4:
            "vec4"
        default:
            "/*unsupported*/"
        }
    }

    var jsonFormatName: String {
        switch self {
        case .FLOAT:
            "float"
        case .FLOAT2:
            "float2"
        case .FLOAT3:
            "float3"
        case .FLOAT4:
            "float4"
        default:
            "float"
        }
    }
}
