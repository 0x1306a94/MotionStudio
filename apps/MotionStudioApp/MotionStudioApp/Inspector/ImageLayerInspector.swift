//
//  ImageLayerInspector.swift
//  MotionStudioApp
//
//  Image-layer controls: asset binding, scale mode, container size, reset.
//

import MotionStudioBridging
import SwiftUI

let imageSizePath = ImageProperty.size.path
let imageCornerRadiusPath = ImageProperty.cornerRadius.path

struct ImageLayerInspector: View {
    let core: MotionDocumentCore
    let layerID: UInt64
    @Environment(PlayheadClock.self) private var clock
    let isEditable: Bool
    let perform: (String, () -> Void) -> Void

    private var playheadFrame: Int64 {
        clock.frame
    }

    var body: some View {
        let _ = core.panelRevision
        let assets = core.imageAssetIDs()
        let boundAssetID = core.imageAssetID(layerID: layerID)
        let selectedAssetName = boundAssetID == 0 ? "None" : core.assetName(boundAssetID)
        let size = core.evaluateVec2(entityID: layerID, path: imageSizePath, frame: playheadFrame)
        let mode = core.imageScaleMode(layerID: layerID)
        let blendMode = core.layerBlendMode(layerID: layerID)

        Text("Image")
            .font(.subheadline)
            .foregroundStyle(.secondary)

        HStack(spacing: 8) {
            Text("Asset")
                .fixedSize(horizontal: true, vertical: false)
            Spacer(minLength: 8)
            Menu {
                Button {
                    setAsset(0)
                } label: {
                    assetOptionLabel("None")
                }
                ForEach(assets, id: \.self) { assetID in
                    Button {
                        setAsset(assetID)
                    } label: {
                        assetOptionLabel(core.assetName(assetID))
                    }
                }
            } label: {
                HStack(spacing: 6) {
                    Text(selectedAssetName)
                        .lineLimit(1)
                        .truncationMode(.middle)
                        .frame(maxWidth: .infinity, alignment: .trailing)
                    Image(systemName: "chevron.up.chevron.down")
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(.secondary)
                }
                .padding(.horizontal, 10)
                .padding(.vertical, 5)
                .frame(width: Self.assetPickerMaxWidth, alignment: .leading)
            }
            .disabled(!isEditable)
            .frame(width: Self.assetPickerMaxWidth, alignment: .trailing)
            .clipped()
        }
        .font(.subheadline)

        HStack(spacing: 8) {
            Text("Scale Mode")
                .fixedSize(horizontal: true, vertical: false)
            Spacer(minLength: 8)
            Picker("Scale Mode", selection: Binding(
                get: { mode },
                set: { newValue in
                    guard isEditable else { return }
                    perform("Set Image Scale Mode") {
                        core.setImageScaleMode(layerID: layerID, mode: newValue)
                    }
                },
            )) {
                ForEach(MS_IMAGE_SCALE.allCases) { option in
                    Text(option.label).tag(option)
                }
            }
            .labelsHidden()
            .pickerStyle(.menu)
            .disabled(!isEditable)
        }
        .font(.subheadline)

        HStack(spacing: 8) {
            Text("Blend Mode")
                .fixedSize(horizontal: true, vertical: false)
            Spacer(minLength: 8)
            Picker("Blend Mode", selection: Binding(
                get: { blendMode == .INVALID ? .NORMAL : blendMode },
                set: { newValue in
                    guard isEditable else { return }
                    perform("Set Layer Blend Mode") {
                        core.setLayerBlendMode(layerID: layerID, blendMode: newValue)
                    }
                },
            )) {
                ForEach(MS_BLEND.allCases) { option in
                    Text(option.label).tag(option)
                }
            }
            .labelsHidden()
            .pickerStyle(.menu)
            .disabled(!isEditable)
        }
        .font(.subheadline)

        NumberPropertyRow(label: "Width",
                          value: Float(size.dx),
                          hasKeyframeAtPlayhead: hasSizeKeyframe(),
                          isEditable: isEditable)
        { newValue in
            setSize(value: CGVector(dx: CGFloat(newValue), dy: size.dy))
        } onToggleKeyframe: { _ in
            toggleSizeKeyframe()
        }

        NumberPropertyRow(label: "Height",
                          value: Float(size.dy),
                          hasKeyframeAtPlayhead: hasSizeKeyframe(),
                          isEditable: isEditable)
        { newValue in
            setSize(value: CGVector(dx: size.dx, dy: CGFloat(newValue)))
        } onToggleKeyframe: { _ in
            toggleSizeKeyframe()
        }

        NumberPropertyRow(label: "Radius",
                          value: core.evaluateFloat(entityID: layerID,
                                                    path: imageCornerRadiusPath,
                                                    frame: playheadFrame),
                          hasKeyframeAtPlayhead: hasCornerRadiusKeyframe(),
                          isEditable: isEditable)
        { newValue in
            setCornerRadius(value: newValue)
        } onToggleKeyframe: { value in
            toggleCornerRadiusKeyframe(value: value)
        }

        Button("Reset to Source Size") {
            guard isEditable, boundAssetID != 0 else { return }
            perform("Reset Image Size") {
                core.resetImageSizeToIntrinsic(layerID: layerID, frame: playheadFrame)
            }
        }
        .disabled(!isEditable || boundAssetID == 0)
        .font(.subheadline)
    }

    private static let assetPickerMaxWidth: CGFloat = 168

    private func assetOptionLabel(_ title: String) -> some View {
        Text(title)
            .lineLimit(1)
            .truncationMode(.middle)
            .frame(maxWidth: Self.assetPickerMaxWidth, alignment: .trailing)
    }

    private func setAsset(_ assetID: UInt64) {
        guard isEditable else { return }
        perform("Set Image Asset") {
            _ = core.setImageAsset(layerID: layerID, assetID: assetID, frame: playheadFrame)
        }
    }

    private func hasSizeKeyframe() -> Bool {
        core.keyframes(entityID: layerID, path: imageSizePath).contains { $0.frame == playheadFrame }
    }

    private func setSize(value: CGVector) {
        guard isEditable else { return }
        perform("Set Image Size") {
            if hasSizeKeyframe() {
                core.addKeyframeVec2(entityID: layerID, path: imageSizePath,
                                     frame: playheadFrame, value: value)
            } else {
                core.setStaticVec2(entityID: layerID, path: imageSizePath, value: value)
            }
        }
    }

    private func toggleSizeKeyframe() {
        guard isEditable else { return }
        if hasSizeKeyframe() {
            perform("Delete Keyframe") {
                core.removeKeyframe(entityID: layerID, path: imageSizePath, frame: playheadFrame)
            }
        } else {
            let value = core.evaluateVec2(entityID: layerID, path: imageSizePath, frame: playheadFrame)
            perform("Add Keyframe") {
                core.addKeyframeVec2(entityID: layerID, path: imageSizePath,
                                     frame: playheadFrame, value: value)
            }
        }
    }

    private func hasCornerRadiusKeyframe() -> Bool {
        core.keyframes(entityID: layerID, path: imageCornerRadiusPath)
            .contains { $0.frame == playheadFrame }
    }

    private func setCornerRadius(value: Float) {
        guard isEditable else { return }
        let radius = max(value, 0)
        perform("Set Corner Radius") {
            if hasCornerRadiusKeyframe() {
                core.addKeyframeFloat(entityID: layerID, path: imageCornerRadiusPath,
                                      frame: playheadFrame, value: radius)
            } else {
                core.setStaticFloat(entityID: layerID, path: imageCornerRadiusPath, value: radius)
            }
        }
    }

    private func toggleCornerRadiusKeyframe(value: Float) {
        guard isEditable else { return }
        if hasCornerRadiusKeyframe() {
            perform("Delete Keyframe") {
                core.removeKeyframe(entityID: layerID, path: imageCornerRadiusPath,
                                    frame: playheadFrame)
            }
        } else {
            perform("Add Keyframe") {
                core.addKeyframeFloat(entityID: layerID, path: imageCornerRadiusPath,
                                      frame: playheadFrame, value: max(value, 0))
            }
        }
    }
}
