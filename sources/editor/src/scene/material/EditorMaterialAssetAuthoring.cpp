#include "scene/material/EditorMaterialAssetAuthoring.hpp"

#include "console/EditorConsoleState.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"

#include <algorithm>
#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] float Clamp01(float value) noexcept {
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] float ClampNonNegative(float value) noexcept {
    return std::max(0.0F, value);
}

[[nodiscard]] kb::render::RenderMaterialAlphaMode NextAlphaMode(kb::render::RenderMaterialAlphaMode mode) noexcept {
    switch (mode) {
    case kb::render::RenderMaterialAlphaMode::Opaque:
        return kb::render::RenderMaterialAlphaMode::Mask;
    case kb::render::RenderMaterialAlphaMode::Mask:
        return kb::render::RenderMaterialAlphaMode::Blend;
    case kb::render::RenderMaterialAlphaMode::Blend:
        return kb::render::RenderMaterialAlphaMode::Opaque;
    }
    return kb::render::RenderMaterialAlphaMode::Opaque;
}

} // namespace

EditorMaterialAssetAuthoring::EditorMaterialAssetAuthoring(kb::scene::Scene& scene, EditorAssetBrowserState& browser, EditorConsoleState& console) noexcept
    : gateway_(scene, browser)
    , console_(console) {}

bool EditorMaterialAssetAuthoring::Create(const std::filesystem::path& virtualFolder) {
    const std::optional<std::filesystem::path> folder = gateway_.ResolveFolder(virtualFolder);
    if (!folder.has_value()) {
        console_.Error("Materials", "Could not resolve a physical folder for the new material.");
        return false;
    }

    const std::filesystem::path path = EditorMaterialAssetGateway::UniqueFilePath(*folder, "NewMaterial");
    kb::render::RenderMaterialAssetData material{};
    if (!gateway_.WriteNewMaterial(path, material)) {
        console_.Error("Materials", "Material asset could not be written: " + path.generic_string());
        return false;
    }

    console_.Info("Materials", "Material asset created: " + path.generic_string());
    return true;
}

std::optional<kb::render::RenderMaterialAssetData> EditorMaterialAssetAuthoring::Read(kb::assets::AssetId id) const {
    return EditorMaterialAssetGateway::Read(gateway_.Scene(), id);
}

bool EditorMaterialAssetAuthoring::SetBaseColor(kb::assets::AssetId id, int channel, float value) {
    if (channel < 0 || channel >= 4) {
        return false;
    }
    const bool saved = gateway_.Mutate(id, [channel, value](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.baseColor[channel] = Clamp01(value);
    });
    if (!saved) {
        console_.Error("Materials", "Material base color could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::SetEmissiveColor(kb::assets::AssetId id, int channel, float value) {
    if (channel < 0 || channel >= 3) {
        return false;
    }
    const bool saved = gateway_.Mutate(id, [channel, value](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.emissiveColor[channel] = ClampNonNegative(value);
    });
    if (!saved) {
        console_.Error("Materials", "Material emissive color could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::SetMetallicFactor(kb::assets::AssetId id, float value) {
    const bool saved = gateway_.Mutate(id, [value](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.metallicFactor = Clamp01(value);
    });
    if (!saved) {
        console_.Error("Materials", "Material metallic factor could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::SetRoughnessFactor(kb::assets::AssetId id, float value) {
    const bool saved = gateway_.Mutate(id, [value](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.roughnessFactor = Clamp01(value);
    });
    if (!saved) {
        console_.Error("Materials", "Material roughness factor could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::SetNormalScale(kb::assets::AssetId id, float value) {
    const bool saved = gateway_.Mutate(id, [value](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.normalScale = ClampNonNegative(value);
    });
    if (!saved) {
        console_.Error("Materials", "Material normal scale could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::SetOcclusionStrength(kb::assets::AssetId id, float value) {
    const bool saved = gateway_.Mutate(id, [value](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.occlusionStrength = Clamp01(value);
    });
    if (!saved) {
        console_.Error("Materials", "Material occlusion strength could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::SetEmissiveStrength(kb::assets::AssetId id, float value) {
    const bool saved = gateway_.Mutate(id, [value](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.emissiveStrength = ClampNonNegative(value);
    });
    if (!saved) {
        console_.Error("Materials", "Material emissive strength could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::SetAlphaCutoff(kb::assets::AssetId id, float value) {
    const bool saved = gateway_.Mutate(id, [value](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.alphaCutoff = Clamp01(value);
    });
    if (!saved) {
        console_.Error("Materials", "Material alpha cutoff could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::SetAlphaMode(kb::assets::AssetId id, kb::render::RenderMaterialAlphaMode mode) {
    const bool saved = gateway_.Mutate(id, [mode](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.alphaMode = mode;
    });
    if (!saved) {
        console_.Error("Materials", "Material alpha mode could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::CycleAlphaMode(kb::assets::AssetId id) {
    const bool saved = gateway_.Mutate(id, [](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.alphaMode = NextAlphaMode(asset.desc.alphaMode);
    });
    if (!saved) {
        console_.Error("Materials", "Material alpha mode could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::ToggleDoubleSided(kb::assets::AssetId id) {
    const bool saved = gateway_.Mutate(id, [](kb::render::RenderMaterialAssetData& asset) {
        asset.desc.doubleSided = !asset.desc.doubleSided;
    });
    if (!saved) {
        console_.Error("Materials", "Material double-sided flag could not be saved.");
    }
    return saved;
}

} // namespace kb::editor
