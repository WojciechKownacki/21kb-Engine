#include "scene/material/EditorMaterialAssetEditCommand.hpp"

#include "engine/scene/Scene.hpp"
#include "scene/material/EditorMaterialAssetGateway.hpp"
#include "scene/material/MaterialEditorState.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

namespace kb::editor {
namespace {

[[nodiscard]] float Clamp01(float value) noexcept {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] float ClampNonNegative(float value) noexcept {
    return std::max(0.0F, value);
}

[[nodiscard]] std::uint64_t& TextureSlot(kb::render::RenderMaterialAssetData& asset, EditorMaterialTextureSlot slot) noexcept {
    switch (slot) {
    case EditorMaterialTextureSlot::Albedo:
        return asset.desc.albedoTextureAssetId;
    case EditorMaterialTextureSlot::Normal:
        return asset.desc.normalTextureAssetId;
    case EditorMaterialTextureSlot::MetallicRoughness:
        return asset.desc.metallicRoughnessTextureAssetId;
    case EditorMaterialTextureSlot::Occlusion:
        return asset.desc.occlusionTextureAssetId;
    case EditorMaterialTextureSlot::Emissive:
        return asset.desc.emissiveTextureAssetId;
    }
    return asset.desc.albedoTextureAssetId;
}

} // namespace

EditorMaterialBaseColorChannelEdit::EditorMaterialBaseColorChannelEdit(int channel, float value) noexcept
    : channel_(channel)
    , value_(value) {}

std::string_view EditorMaterialBaseColorChannelEdit::Label() const noexcept {
    return "Edit Material Base Color";
}

void EditorMaterialBaseColorChannelEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    if (channel_ >= 0 && channel_ < 4) {
        asset.desc.baseColor[channel_] = Clamp01(value_);
    }
}

EditorMaterialEmissiveColorChannelEdit::EditorMaterialEmissiveColorChannelEdit(int channel, float value) noexcept
    : channel_(channel)
    , value_(value) {}

std::string_view EditorMaterialEmissiveColorChannelEdit::Label() const noexcept {
    return "Edit Material Emissive Color";
}

void EditorMaterialEmissiveColorChannelEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    if (channel_ >= 0 && channel_ < 3) {
        asset.desc.emissiveColor[channel_] = ClampNonNegative(value_);
    }
}

EditorMaterialMetallicFactorEdit::EditorMaterialMetallicFactorEdit(float value) noexcept
    : value_(value) {}

std::string_view EditorMaterialMetallicFactorEdit::Label() const noexcept {
    return "Edit Material Metallic";
}

void EditorMaterialMetallicFactorEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    asset.desc.metallicFactor = Clamp01(value_);
}

EditorMaterialRoughnessFactorEdit::EditorMaterialRoughnessFactorEdit(float value) noexcept
    : value_(value) {}

std::string_view EditorMaterialRoughnessFactorEdit::Label() const noexcept {
    return "Edit Material Roughness";
}

void EditorMaterialRoughnessFactorEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    asset.desc.roughnessFactor = Clamp01(value_);
}

EditorMaterialNormalScaleEdit::EditorMaterialNormalScaleEdit(float value) noexcept
    : value_(value) {}

std::string_view EditorMaterialNormalScaleEdit::Label() const noexcept {
    return "Edit Material Normal Scale";
}

void EditorMaterialNormalScaleEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    asset.desc.normalScale = ClampNonNegative(value_);
}

EditorMaterialOcclusionStrengthEdit::EditorMaterialOcclusionStrengthEdit(float value) noexcept
    : value_(value) {}

std::string_view EditorMaterialOcclusionStrengthEdit::Label() const noexcept {
    return "Edit Material Occlusion";
}

void EditorMaterialOcclusionStrengthEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    asset.desc.occlusionStrength = Clamp01(value_);
}

EditorMaterialEmissiveStrengthEdit::EditorMaterialEmissiveStrengthEdit(float value) noexcept
    : value_(value) {}

std::string_view EditorMaterialEmissiveStrengthEdit::Label() const noexcept {
    return "Edit Material Emissive Strength";
}

void EditorMaterialEmissiveStrengthEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    asset.desc.emissiveStrength = ClampNonNegative(value_);
}

EditorMaterialAlphaCutoffEdit::EditorMaterialAlphaCutoffEdit(float value) noexcept
    : value_(value) {}

std::string_view EditorMaterialAlphaCutoffEdit::Label() const noexcept {
    return "Edit Material Alpha Cutoff";
}

void EditorMaterialAlphaCutoffEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    asset.desc.alphaCutoff = Clamp01(value_);
}

EditorMaterialAlphaModeEdit::EditorMaterialAlphaModeEdit(kb::render::RenderMaterialAlphaMode mode) noexcept
    : mode_(mode) {}

std::string_view EditorMaterialAlphaModeEdit::Label() const noexcept {
    return "Edit Material Alpha Mode";
}

void EditorMaterialAlphaModeEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    asset.desc.alphaMode = mode_;
}

EditorMaterialDoubleSidedEdit::EditorMaterialDoubleSidedEdit(bool doubleSided) noexcept
    : doubleSided_(doubleSided) {}

std::string_view EditorMaterialDoubleSidedEdit::Label() const noexcept {
    return "Edit Material Double Sided";
}

void EditorMaterialDoubleSidedEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    asset.desc.doubleSided = doubleSided_;
}

EditorMaterialTextureAssetEdit::EditorMaterialTextureAssetEdit(EditorMaterialTextureSlot slot, kb::assets::AssetId textureId) noexcept
    : slot_(slot)
    , textureId_(textureId) {}

std::string_view EditorMaterialTextureAssetEdit::Label() const noexcept {
    return "Edit Material Texture";
}

void EditorMaterialTextureAssetEdit::Apply(kb::render::RenderMaterialAssetData& asset) const {
    TextureSlot(asset, slot_) = textureId_.value;
}

std::unique_ptr<EditorMaterialAssetEditCommand> EditorMaterialAssetEditCommand::Create(
    kb::scene::Scene& scene,
    kb::assets::AssetId materialId,
    std::unique_ptr<IEditorMaterialAssetPropertyEdit> edit) {
    if (edit == nullptr) {
        return {};
    }

    std::optional<kb::render::RenderMaterialAssetData> before = EditorMaterialAssetGateway::Read(scene, materialId);
    if (!before.has_value()) {
        return {};
    }

    kb::render::RenderMaterialAssetData after = *before;
    edit->Apply(after);
    return std::unique_ptr<EditorMaterialAssetEditCommand>{ new EditorMaterialAssetEditCommand{
        scene,
        materialId,
        std::string{ edit->Label() },
        std::move(*before),
        std::move(after),
    } };
}

std::unique_ptr<EditorMaterialAssetEditCommand> EditorMaterialAssetEditCommand::CreateRecorded(
    kb::scene::Scene& scene,
    kb::assets::AssetId materialId,
    std::string label,
    kb::render::RenderMaterialAssetData before,
    kb::render::RenderMaterialAssetData after) {
    return std::unique_ptr<EditorMaterialAssetEditCommand>{ new EditorMaterialAssetEditCommand{
        scene,
        materialId,
        std::move(label),
        std::move(before),
        std::move(after),
    } };
}

EditorMaterialAssetEditCommand::EditorMaterialAssetEditCommand(
    kb::scene::Scene& scene,
    kb::assets::AssetId materialId,
    std::string label,
    kb::render::RenderMaterialAssetData before,
    kb::render::RenderMaterialAssetData after)
    : scene_(scene)
    , materialId_(materialId)
    , label_(std::move(label))
    , before_(std::move(before))
    , after_(std::move(after)) {}

std::string_view EditorMaterialAssetEditCommand::Label() const noexcept {
    return label_;
}

bool EditorMaterialAssetEditCommand::AffectsSceneDocument() const noexcept {
    return false;
}

bool EditorMaterialAssetEditCommand::AffectsHierarchySelection() const noexcept {
    return false;
}

bool EditorMaterialAssetEditCommand::AffectsOpenMaterialSource() const noexcept {
    return true;
}

bool EditorMaterialAssetEditCommand::Execute() {
    return Write(after_);
}

bool EditorMaterialAssetEditCommand::Undo() {
    return Write(before_);
}

bool EditorMaterialAssetEditCommand::Redo() {
    return Write(after_);
}

bool EditorMaterialAssetEditCommand::Write(const kb::render::RenderMaterialAssetData& asset) {
    return EditorMaterialAssetGateway::WriteExisting(scene_, materialId_, asset);
}

std::unique_ptr<EditorMaterialWorkingCopyEditCommand> EditorMaterialWorkingCopyEditCommand::Create(
    MaterialEditorState& editor,
    kb::assets::AssetId materialId,
    std::string label,
    kb::render::RenderMaterialAssetData before,
    kb::render::RenderMaterialAssetData after,
    std::uint32_t beforeSelectedNodeId,
    std::uint32_t afterSelectedNodeId) {
    return std::unique_ptr<EditorMaterialWorkingCopyEditCommand>{ new EditorMaterialWorkingCopyEditCommand{
        editor,
        materialId,
        std::move(label),
        std::move(before),
        std::move(after),
        beforeSelectedNodeId,
        afterSelectedNodeId,
    } };
}

EditorMaterialWorkingCopyEditCommand::EditorMaterialWorkingCopyEditCommand(
    MaterialEditorState& editor,
    kb::assets::AssetId materialId,
    std::string label,
    kb::render::RenderMaterialAssetData before,
    kb::render::RenderMaterialAssetData after,
    std::uint32_t beforeSelectedNodeId,
    std::uint32_t afterSelectedNodeId)
    : editor_(editor)
    , materialId_(materialId)
    , label_(std::move(label))
    , before_(std::move(before))
    , after_(std::move(after))
    , beforeSelectedNodeId_(beforeSelectedNodeId)
    , afterSelectedNodeId_(afterSelectedNodeId) {}

std::string_view EditorMaterialWorkingCopyEditCommand::Label() const noexcept {
    return label_;
}

bool EditorMaterialWorkingCopyEditCommand::AffectsSceneDocument() const noexcept {
    return false;
}

bool EditorMaterialWorkingCopyEditCommand::AffectsHierarchySelection() const noexcept {
    return false;
}

bool EditorMaterialWorkingCopyEditCommand::Execute() {
    return Apply(after_, afterSelectedNodeId_);
}

bool EditorMaterialWorkingCopyEditCommand::Undo() {
    return Apply(before_, beforeSelectedNodeId_);
}

bool EditorMaterialWorkingCopyEditCommand::Redo() {
    return Apply(after_, afterSelectedNodeId_);
}

bool EditorMaterialWorkingCopyEditCommand::Apply(const kb::render::RenderMaterialAssetData& asset, std::uint32_t selectedNodeId) {
    if (editor_.OpenAssetId() != materialId_ || !editor_.WorkingCopy().has_value()) {
        return false;
    }

    editor_.SetWorkingCopy(asset);
    if (selectedNodeId != 0U && kb::render::FindRenderMaterialGraphNode(asset.graph, selectedNodeId) != nullptr) {
        static_cast<void>(editor_.SelectNode(selectedNodeId));
    } else {
        static_cast<void>(editor_.ClearNodeSelection());
    }
    return true;
}

} // namespace kb::editor
