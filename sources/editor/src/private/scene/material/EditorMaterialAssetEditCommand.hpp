#pragma once

#include "commands/IEditorCommand.hpp"
#include "engine/assets/AssetId.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"
#include "scene/material/EditorMaterialAssetAuthoring.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace kb::scene {
class Scene;
}

namespace kb::editor {

class MaterialEditorState;

class IEditorMaterialAssetPropertyEdit {
public:
    virtual ~IEditorMaterialAssetPropertyEdit() = default;

    [[nodiscard]] virtual std::string_view Label() const noexcept = 0;
    virtual void Apply(kb::render::RenderMaterialAssetData& asset) const = 0;
};

class EditorMaterialBaseColorChannelEdit final : public IEditorMaterialAssetPropertyEdit {
public:
    EditorMaterialBaseColorChannelEdit(int channel, float value) noexcept;
    [[nodiscard]] std::string_view Label() const noexcept override;
    void Apply(kb::render::RenderMaterialAssetData& asset) const override;

private:
    int channel_ = 0;
    float value_ = 0.0F;
};

class EditorMaterialEmissiveColorChannelEdit final : public IEditorMaterialAssetPropertyEdit {
public:
    EditorMaterialEmissiveColorChannelEdit(int channel, float value) noexcept;
    [[nodiscard]] std::string_view Label() const noexcept override;
    void Apply(kb::render::RenderMaterialAssetData& asset) const override;

private:
    int channel_ = 0;
    float value_ = 0.0F;
};

class EditorMaterialMetallicFactorEdit final : public IEditorMaterialAssetPropertyEdit {
public:
    explicit EditorMaterialMetallicFactorEdit(float value) noexcept;
    [[nodiscard]] std::string_view Label() const noexcept override;
    void Apply(kb::render::RenderMaterialAssetData& asset) const override;

private:
    float value_ = 0.0F;
};

class EditorMaterialRoughnessFactorEdit final : public IEditorMaterialAssetPropertyEdit {
public:
    explicit EditorMaterialRoughnessFactorEdit(float value) noexcept;
    [[nodiscard]] std::string_view Label() const noexcept override;
    void Apply(kb::render::RenderMaterialAssetData& asset) const override;

private:
    float value_ = 0.0F;
};

class EditorMaterialNormalScaleEdit final : public IEditorMaterialAssetPropertyEdit {
public:
    explicit EditorMaterialNormalScaleEdit(float value) noexcept;
    [[nodiscard]] std::string_view Label() const noexcept override;
    void Apply(kb::render::RenderMaterialAssetData& asset) const override;

private:
    float value_ = 0.0F;
};

class EditorMaterialOcclusionStrengthEdit final : public IEditorMaterialAssetPropertyEdit {
public:
    explicit EditorMaterialOcclusionStrengthEdit(float value) noexcept;
    [[nodiscard]] std::string_view Label() const noexcept override;
    void Apply(kb::render::RenderMaterialAssetData& asset) const override;

private:
    float value_ = 0.0F;
};

class EditorMaterialEmissiveStrengthEdit final : public IEditorMaterialAssetPropertyEdit {
public:
    explicit EditorMaterialEmissiveStrengthEdit(float value) noexcept;
    [[nodiscard]] std::string_view Label() const noexcept override;
    void Apply(kb::render::RenderMaterialAssetData& asset) const override;

private:
    float value_ = 0.0F;
};

class EditorMaterialAlphaCutoffEdit final : public IEditorMaterialAssetPropertyEdit {
public:
    explicit EditorMaterialAlphaCutoffEdit(float value) noexcept;
    [[nodiscard]] std::string_view Label() const noexcept override;
    void Apply(kb::render::RenderMaterialAssetData& asset) const override;

private:
    float value_ = 0.0F;
};

class EditorMaterialAlphaModeEdit final : public IEditorMaterialAssetPropertyEdit {
public:
    explicit EditorMaterialAlphaModeEdit(kb::render::RenderMaterialAlphaMode mode) noexcept;
    [[nodiscard]] std::string_view Label() const noexcept override;
    void Apply(kb::render::RenderMaterialAssetData& asset) const override;

private:
    kb::render::RenderMaterialAlphaMode mode_ = kb::render::RenderMaterialAlphaMode::Opaque;
};

class EditorMaterialDoubleSidedEdit final : public IEditorMaterialAssetPropertyEdit {
public:
    explicit EditorMaterialDoubleSidedEdit(bool doubleSided) noexcept;
    [[nodiscard]] std::string_view Label() const noexcept override;
    void Apply(kb::render::RenderMaterialAssetData& asset) const override;

private:
    bool doubleSided_ = false;
};

class EditorMaterialTextureAssetEdit final : public IEditorMaterialAssetPropertyEdit {
public:
    EditorMaterialTextureAssetEdit(EditorMaterialTextureSlot slot, kb::assets::AssetId textureId) noexcept;
    [[nodiscard]] std::string_view Label() const noexcept override;
    void Apply(kb::render::RenderMaterialAssetData& asset) const override;

private:
    EditorMaterialTextureSlot slot_ = EditorMaterialTextureSlot::Albedo;
    kb::assets::AssetId textureId_{};
};

[[nodiscard]] bool ApplyEditorMaterialOutputNormalTextureGraph(
    kb::render::RenderMaterialAssetData& material,
    kb::assets::AssetId textureId);

class EditorMaterialAssetEditCommand final : public IEditorCommand {
public:
    [[nodiscard]] static std::unique_ptr<EditorMaterialAssetEditCommand> Create(
        kb::scene::Scene& scene,
        kb::assets::AssetId materialId,
        std::unique_ptr<IEditorMaterialAssetPropertyEdit> edit);
    [[nodiscard]] static std::unique_ptr<EditorMaterialAssetEditCommand> CreateRecorded(
        kb::scene::Scene& scene,
        kb::assets::AssetId materialId,
        std::string label,
        kb::render::RenderMaterialAssetData before,
        kb::render::RenderMaterialAssetData after);

    [[nodiscard]] std::string_view Label() const noexcept override;
    [[nodiscard]] bool AffectsSceneDocument() const noexcept override;
    [[nodiscard]] bool AffectsHierarchySelection() const noexcept override;
    [[nodiscard]] bool AffectsOpenMaterialSource() const noexcept override;
    [[nodiscard]] EditorCommandHistoryKey HistoryKey() const noexcept override;
    [[nodiscard]] bool Execute() override;
    [[nodiscard]] bool Undo() override;
    [[nodiscard]] bool Redo() override;

private:
    EditorMaterialAssetEditCommand(
        kb::scene::Scene& scene,
        kb::assets::AssetId materialId,
        std::string label,
        kb::render::RenderMaterialAssetData before,
        kb::render::RenderMaterialAssetData after);

    [[nodiscard]] bool Write(const kb::render::RenderMaterialAssetData& asset);

    kb::scene::Scene& scene_;
    kb::assets::AssetId materialId_{};
    std::string label_;
    kb::render::RenderMaterialAssetData before_;
    kb::render::RenderMaterialAssetData after_;
};

class EditorMaterialInstanceEditCommand final : public IEditorCommand {
public:
    [[nodiscard]] static std::unique_ptr<EditorMaterialInstanceEditCommand> CreateRecorded(
        kb::scene::Scene& scene,
        kb::assets::AssetId materialInstanceId,
        std::string label,
        kb::render::RenderMaterialInstanceAssetData before,
        kb::render::RenderMaterialInstanceAssetData after);

    [[nodiscard]] std::string_view Label() const noexcept override;
    [[nodiscard]] bool AffectsSceneDocument() const noexcept override;
    [[nodiscard]] bool AffectsHierarchySelection() const noexcept override;
    [[nodiscard]] bool AffectsOpenMaterialSource() const noexcept override;
    [[nodiscard]] EditorCommandHistoryKey HistoryKey() const noexcept override;
    [[nodiscard]] bool Execute() override;
    [[nodiscard]] bool Undo() override;
    [[nodiscard]] bool Redo() override;

private:
    EditorMaterialInstanceEditCommand(
        kb::scene::Scene& scene,
        kb::assets::AssetId materialInstanceId,
        std::string label,
        kb::render::RenderMaterialInstanceAssetData before,
        kb::render::RenderMaterialInstanceAssetData after);

    [[nodiscard]] bool Write(const kb::render::RenderMaterialInstanceAssetData& asset);

    kb::scene::Scene& scene_;
    kb::assets::AssetId materialInstanceId_{};
    std::string label_;
    kb::render::RenderMaterialInstanceAssetData before_;
    kb::render::RenderMaterialInstanceAssetData after_;
};

class EditorMaterialWorkingCopyEditCommand final : public IEditorCommand {
public:
    [[nodiscard]] static std::unique_ptr<EditorMaterialWorkingCopyEditCommand> Create(
        MaterialEditorState& editor,
        kb::assets::AssetId materialId,
        std::string label,
        kb::render::RenderMaterialAssetData before,
        kb::render::RenderMaterialAssetData after,
        std::uint32_t beforeSelectedNodeId,
        std::uint32_t afterSelectedNodeId);
    [[nodiscard]] static std::unique_ptr<EditorMaterialWorkingCopyEditCommand> Create(
        MaterialEditorState& editor,
        kb::assets::AssetId materialId,
        std::string label,
        kb::render::RenderMaterialAssetData before,
        kb::render::RenderMaterialAssetData after,
        std::vector<std::uint32_t> beforeSelectedNodeIds,
        std::vector<std::uint32_t> afterSelectedNodeIds,
        std::uint32_t beforeSelectedCommentId = 0U,
        std::uint32_t afterSelectedCommentId = 0U);

    [[nodiscard]] std::string_view Label() const noexcept override;
    [[nodiscard]] bool AffectsSceneDocument() const noexcept override;
    [[nodiscard]] bool AffectsHierarchySelection() const noexcept override;
    [[nodiscard]] EditorCommandHistoryKey HistoryKey() const noexcept override;
    [[nodiscard]] bool Execute() override;
    [[nodiscard]] bool Undo() override;
    [[nodiscard]] bool Redo() override;

private:
    EditorMaterialWorkingCopyEditCommand(
        MaterialEditorState& editor,
        kb::assets::AssetId materialId,
        std::string label,
        kb::render::RenderMaterialAssetData before,
        kb::render::RenderMaterialAssetData after,
        std::vector<std::uint32_t> beforeSelectedNodeIds,
        std::vector<std::uint32_t> afterSelectedNodeIds,
        std::uint32_t beforeSelectedCommentId,
        std::uint32_t afterSelectedCommentId);

    [[nodiscard]] bool Apply(
        const kb::render::RenderMaterialAssetData& asset,
        const std::vector<std::uint32_t>& selectedNodeIds,
        std::uint32_t selectedCommentId);

    MaterialEditorState& editor_;
    kb::assets::AssetId materialId_{};
    std::string label_;
    kb::render::RenderMaterialAssetData before_;
    kb::render::RenderMaterialAssetData after_;
    std::vector<std::uint32_t> beforeSelectedNodeIds_;
    std::vector<std::uint32_t> afterSelectedNodeIds_;
    std::uint32_t beforeSelectedCommentId_ = 0U;
    std::uint32_t afterSelectedCommentId_ = 0U;
};

} // namespace kb::editor
