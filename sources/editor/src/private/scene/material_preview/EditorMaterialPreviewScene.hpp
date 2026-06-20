#pragma once

#include "engine/assets/AssetId.hpp"
#include "engine/scene/Scene.hpp"
#include "scene/material_preview/EditorMaterialPreviewTelemetry.hpp"

#include <cstdint>
#include <memory>

namespace kb::editor {

class EditorMaterialPreviewScene {
public:
    [[nodiscard]] const kb::scene::Scene& SceneFor(const kb::scene::Scene& sourceScene, kb::assets::AssetId materialAssetId);
    [[nodiscard]] const EditorMaterialPreviewTelemetry& Telemetry() const noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept;
    void Clear() noexcept;

private:
    void Rebuild(const kb::scene::Scene& sourceScene, kb::assets::AssetId materialAssetId);

    std::unique_ptr<kb::scene::Scene> scene_;
    EditorMaterialPreviewTelemetry telemetry_{};
    kb::assets::AssetId materialAssetId_{};
    std::uint64_t sourceAssetRevision_ = 0U;
    std::uint64_t materialContentHash_ = 0U;
    std::uint64_t revision_ = 1U;
};

} // namespace kb::editor
