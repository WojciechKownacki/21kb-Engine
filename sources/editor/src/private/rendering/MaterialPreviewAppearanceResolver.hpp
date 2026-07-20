#pragma once

#include "engine/assets/AssetManager.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"

#include <array>
#include <optional>

namespace kb::editor {

struct MaterialPreviewAppearance {
    float baseColor[3] = { 1.0F, 1.0F, 1.0F };
    float emissiveColor[3] = { 0.0F, 0.0F, 0.0F };
    float roughness = 0.65F;
    float metallic = 0.0F;
    float emissiveStrength = 0.0F;
    // True when the values above came from the graph rather than from the PBR descriptor.
    bool fromGraph = false;
};

// The static (software shaded) material previews - the Inspector ball and the Project Files tile - used to
// read only material.desc. A graph-backed material leaves those fields at their white fallbacks, so every
// material authored in the graph rendered as a plain white sphere no matter what it actually looks like.
// This resolves what the graph feeds into Material Output: constant colours directly, and texture samples
// through the average colour of the texture the node points at.
// Supplied by the editor target (it owns the texture decoder); without it a texture-fed input simply keeps
// the descriptor value instead of guessing a colour.
using MaterialPreviewTextureAverageColorFn =
    std::optional<std::array<float, 3U>> (*)(const kb::assets::AssetManager&, kb::assets::AssetId);

class MaterialPreviewAppearanceResolver {
public:
    MaterialPreviewAppearanceResolver() = delete;

    [[nodiscard]] static MaterialPreviewAppearance Resolve(
        const kb::render::RenderMaterialAssetData& material,
        const kb::assets::AssetManager* assets,
        MaterialPreviewTextureAverageColorFn textureAverageColor = nullptr);
};

} // namespace kb::editor
