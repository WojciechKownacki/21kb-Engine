#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/Scene.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "scene/material_preview/EditorMaterialPreviewPrimitivePolicy.hpp"
#include "scene/material_preview/EditorMaterialPreviewTelemetry.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace kb::editor {

class EditorMaterialPreviewScene {
public:
    ~EditorMaterialPreviewScene();

    [[nodiscard]] const kb::scene::Scene& SceneFor(
        const kb::scene::Scene& sourceScene,
        kb::assets::AssetId materialAssetId,
        const kb::render::RenderMaterialAssetData* workingCopy = nullptr);
    [[nodiscard]] const EditorMaterialPreviewPrimitivePolicy& PrimitivePolicy() const noexcept;
    bool SetPrimitivePolicy(EditorMaterialPreviewPrimitivePolicy policy) noexcept;
    [[nodiscard]] const EditorMaterialPreviewTelemetry& Telemetry() const noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept;
    void Clear() noexcept;

private:
    void Rebuild(
        const kb::scene::Scene& sourceScene,
        kb::assets::AssetId materialAssetId,
        const kb::render::RenderMaterialAssetData* workingCopy,
        std::uint64_t contentHash);

    std::unique_ptr<kb::scene::Scene> scene_;
    EditorMaterialPreviewTelemetry telemetry_{};
    kb::assets::AssetId materialAssetId_{};
    std::filesystem::path workingCopyPath_;
    EditorMaterialPreviewPrimitivePolicy primitivePolicy_ = EditorMaterialPreviewPrimitivePolicy::Sphere();
    std::uint64_t materialContentHash_ = 0U;
    std::uint64_t revision_ = 1U;
};

} // namespace kb::editor
