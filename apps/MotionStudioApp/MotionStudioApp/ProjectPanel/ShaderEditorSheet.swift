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
    var animatable: Bool = true
    var defaultFloat: Float = 0
    var defaultFloat2: SIMD2<Float> = .zero
    var defaultFloat3: SIMD3<Float> = .zero
    var defaultFloat4: SIMD4<Float> = .zero
    var defaultColor: MotionColor = .init(r: 1, g: 1, b: 1, a: 1)
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
    @State private var draftAnimatable = true
    @State private var draftDefaultFloat: Float = 0
    @State private var draftDefaultFloat2: SIMD2<Float> = .zero
    @State private var draftDefaultFloat3: SIMD3<Float> = .zero
    @State private var draftDefaultFloat4: SIMD4<Float> = .zero
    @State private var draftDefaultColor = MotionColor(r: 1, g: 1, b: 1, a: 1)

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
            let resolved = format == .INVALID ? .FLOAT : format
            drafts.append(Self.makeDraft(from: core, shaderID: shaderID, index: index, format: resolved))
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
                            if !uniform.animatable {
                                Text("static")
                                    .font(.caption2)
                                    .foregroundStyle(.secondary)
                            }
                            Spacer(minLength: 0)
                            Button("Edit") {
                                loadDraft(from: uniform)
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
                        resetDraftDefaults(for: .FLOAT)
                        draftUniformName = ""
                        draftUniformFormat = .FLOAT
                        draftAnimatable = true
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
                        Text(format.editorLabel).tag(format)
                    }
                }
                .onChange(of: draftUniformFormat) { _, newFormat in
                    resetDraftDefaults(for: newFormat)
                }
                Toggle("Animatable", isOn: $draftAnimatable)
                defaultValueSection
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
        .presentationDetents([.medium, .large])
    }

    @ViewBuilder
    private var defaultValueSection: some View {
        switch draftUniformFormat {
        case .FLOAT:
            HStack {
                Text("Default")
                Spacer()
                TextField("0", value: $draftDefaultFloat, format: .number)
                    .multilineTextAlignment(.trailing)
                    .keyboardType(.decimalPad)
                    .frame(maxWidth: 120)
            }
        case .FLOAT2:
            defaultAxisRow("Default X", value: float2Binding(\.x))
            defaultAxisRow("Default Y", value: float2Binding(\.y))
        case .FLOAT3:
            defaultAxisRow("Default X", value: float3Binding(\.x))
            defaultAxisRow("Default Y", value: float3Binding(\.y))
            defaultAxisRow("Default Z", value: float3Binding(\.z))
        case .FLOAT4:
            defaultAxisRow("Default X", value: float4Binding(\.x))
            defaultAxisRow("Default Y", value: float4Binding(\.y))
            defaultAxisRow("Default Z", value: float4Binding(\.z))
            defaultAxisRow("Default W", value: float4Binding(\.w))
        case .COLOR:
            ColorPicker("Default", selection: draftDefaultColorBinding, supportsOpacity: true)
        default:
            EmptyView()
        }
    }

    private func defaultAxisRow(_ title: String, value: Binding<Float>) -> some View {
        HStack {
            Text(title)
            Spacer()
            TextField("0", value: value, format: .number)
                .multilineTextAlignment(.trailing)
                .keyboardType(.decimalPad)
                .frame(maxWidth: 120)
        }
    }

    private func float2Binding(_ keyPath: WritableKeyPath<SIMD2<Float>, Float>) -> Binding<Float> {
        Binding(
            get: { draftDefaultFloat2[keyPath: keyPath] },
            set: { draftDefaultFloat2[keyPath: keyPath] = $0 },
        )
    }

    private func float3Binding(_ keyPath: WritableKeyPath<SIMD3<Float>, Float>) -> Binding<Float> {
        Binding(
            get: { draftDefaultFloat3[keyPath: keyPath] },
            set: { draftDefaultFloat3[keyPath: keyPath] = $0 },
        )
    }

    private func float4Binding(_ keyPath: WritableKeyPath<SIMD4<Float>, Float>) -> Binding<Float> {
        Binding(
            get: { draftDefaultFloat4[keyPath: keyPath] },
            set: { draftDefaultFloat4[keyPath: keyPath] = $0 },
        )
    }

    private var draftDefaultColorBinding: Binding<Color> {
        Binding {
            draftDefaultColor.swiftUIColor
        } set: { newValue in
            draftDefaultColor = MotionColor(newValue).clampedChannels()
        }
    }

    private func loadDraft(from uniform: ShaderUniformDraft) {
        draftUniformName = uniform.name
        draftUniformFormat = uniform.format
        draftAnimatable = uniform.animatable
        draftDefaultFloat = uniform.defaultFloat
        draftDefaultFloat2 = uniform.defaultFloat2
        draftDefaultFloat3 = uniform.defaultFloat3
        draftDefaultFloat4 = uniform.defaultFloat4
        draftDefaultColor = uniform.defaultColor
    }

    private func resetDraftDefaults(for format: MS_UNIFORM_FORMAT) {
        draftDefaultFloat = 0
        draftDefaultFloat2 = .zero
        draftDefaultFloat3 = .zero
        draftDefaultFloat4 = .zero
        draftDefaultColor = MotionColor(r: 1, g: 1, b: 1, a: 1)
        _ = format
    }

    private func applyUniformDraft(isNew: Bool, existingID: UUID?) {
        let trimmed = draftUniformName.trimmingCharacters(in: .whitespacesAndNewlines)
        guard isValidUniformName(trimmed, excluding: existingID) else { return }
        var draft = ShaderUniformDraft(name: trimmed, format: draftUniformFormat)
        draft.animatable = draftAnimatable
        draft.defaultFloat = draftDefaultFloat
        draft.defaultFloat2 = draftDefaultFloat2
        draft.defaultFloat3 = draftDefaultFloat3
        draft.defaultFloat4 = draftDefaultFloat4
        draft.defaultColor = draftDefaultColor
        if isNew {
            uniforms.append(draft)
            showAddUniform = false
        } else if let existingID,
                  let index = uniforms.firstIndex(where: { $0.id == existingID })
        {
            draft.id = existingID
            uniforms[index] = draft
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
        var didUpdate = false
        perform("Update Shader") {
            didUpdate = core.updateShader(id: shaderID, name: trimmedName, mainImage: mainImage,
                                          uniformsJSON: json)
        }
        guard didUpdate else {
            return
        }
        // Dismiss on the next turn so parent panels can process the revision bump
        // before the sheet tears down (Menu pickers / sibling hosts).
        Task { @MainActor in
            onDismiss()
        }
    }

    private static func makeDraft(from core: MotionDocumentCore, shaderID: UInt64, index: Int,
                                  format: MS_UNIFORM_FORMAT) -> ShaderUniformDraft
    {
        var draft = ShaderUniformDraft(name: core.shaderUniformName(shaderID, index: index),
                                       format: format)
        draft.animatable = core.shaderUniformAnimatable(shaderID, index: index)
        switch format {
        case .FLOAT:
            draft.defaultFloat = core.shaderUniformDefaultFloat(shaderID, index: index)
        case .FLOAT2:
            let value = core.shaderUniformDefaultVec2(shaderID, index: index)
            draft.defaultFloat2 = SIMD2(Float(value.dx), Float(value.dy))
        case .FLOAT3:
            draft.defaultFloat3 = core.shaderUniformDefaultVec3(shaderID, index: index)
        case .FLOAT4:
            draft.defaultFloat4 = core.shaderUniformDefaultVec4(shaderID, index: index)
        case .COLOR:
            draft.defaultColor = core.shaderUniformDefaultColor(shaderID, index: index)
        default:
            break
        }
        return draft
    }

    private static func encodeUniformsJSON(_ uniforms: [ShaderUniformDraft]) -> String? {
        let objects: [[String: Any]] = uniforms.map { uniform in
            var object: [String: Any] = [
                "name": uniform.name,
                "format": uniform.format.jsonFormatName,
                "count": 1,
                "animatable": uniform.animatable,
            ]
            object["default"] = defaultJSON(for: uniform)
            return object
        }
        guard let data = try? JSONSerialization.data(withJSONObject: objects, options: []),
              let text = String(data: data, encoding: .utf8)
        else {
            return nil
        }
        return text
    }

    private static func defaultJSON(for uniform: ShaderUniformDraft) -> Any {
        switch uniform.format {
        case .FLOAT:
            uniform.defaultFloat
        case .FLOAT2:
            [uniform.defaultFloat2.x, uniform.defaultFloat2.y]
        case .FLOAT3:
            [uniform.defaultFloat3.x, uniform.defaultFloat3.y, uniform.defaultFloat3.z]
        case .FLOAT4:
            [uniform.defaultFloat4.x, uniform.defaultFloat4.y, uniform.defaultFloat4.z,
             uniform.defaultFloat4.w]
        case .COLOR:
            String(format: "#%02X%02X%02X%02X",
                   Int((uniform.defaultColor.r * 255).rounded()),
                   Int((uniform.defaultColor.g * 255).rounded()),
                   Int((uniform.defaultColor.b * 255).rounded()),
                   Int((uniform.defaultColor.a * 255).rounded()))
        default:
            0
        }
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
        [.FLOAT, .FLOAT2, .FLOAT3, .FLOAT4, .COLOR]
    }

    var editorLabel: String {
        switch self {
        case .COLOR:
            "color"
        default:
            glslTypeName
        }
    }

    var glslTypeName: String {
        switch self {
        case .FLOAT:
            "float"
        case .FLOAT2:
            "vec2"
        case .FLOAT3:
            "vec3"
        case .FLOAT4, .COLOR:
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
        case .COLOR:
            "color"
        default:
            "float"
        }
    }
}
