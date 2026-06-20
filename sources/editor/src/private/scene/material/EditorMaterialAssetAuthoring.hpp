#pragma once

#include "engine/assets/AssetId.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "scene/material/EditorMaterialAssetGateway.hpp"

#include <filesystem>
#include <optional>

namespace kb::editor {

class EditorConsoleState;

class EditorMaterialAssetAuthoring {
public:
    EditorMaterialAssetAuthoring(kb::scene::Scene& scene, EditorAssetBrowserState& browser, EditorConsoleState& console) noexcept;

    [[nodiscard]] bool Create(const std::filesystem::path& virtualFolder);
    [[nodiscard]] std::optional<kb::render::RenderMaterialAssetData> Read(kb::assets::AssetId id) const;
    [[nodiscard]] bool SetBaseColor(kb::assets::AssetId id, int channel, float value);
    [[nodiscard]] bool SetEmissiveColor(kb::assets::AssetId id, int channel, float value);
    [[nodiscard]] bool SetMetallicFactor(kb::assets::AssetId id, float value);
    [[nodiscard]] bool SetRoughnessFactor(kb::assets::AssetId id, float value);
    [[nodiscard]] bool SetNormalScale(kb::assets::AssetId id, float value);
    [[nodiscard]] bool SetOcclusionStrength(kb::assets::AssetId id, float value);
    [[nodiscard]] bool SetEmissiveStrength(kb::assets::AssetId id, float value);
    [[nodiscard]] bool SetAlphaCutoff(kb::assets::AssetId id, float value);
    [[nodiscard]] bool SetAlphaMode(kb::assets::AssetId id, kb::render::RenderMaterialAlphaMode mode);
    [[nodiscard]] bool CycleAlphaMode(kb::assets::AssetId id);
    [[nodiscard]] bool ToggleDoubleSided(kb::assets::AssetId id);

private:
    EditorMaterialAssetGateway gateway_;
    EditorConsoleState& console_;
};

} // namespace kb::editor
