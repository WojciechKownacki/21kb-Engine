#include "scene/material/EditorMaterialAssetAuthoring.hpp"

#include "console/EditorConsoleState.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialInstanceAssetLoader.hpp"

#include <algorithm>
#include <optional>
#include <vector>

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

[[nodiscard]] bool IsTextureAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderTexture" || metadata.type == "Texture" || metadata.importCategory == "Texture";
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

[[nodiscard]] std::uint64_t TextureSlotValue(const kb::render::RenderMaterialAssetData& asset, EditorMaterialTextureSlot slot) noexcept {
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
    return 0U;
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
    material.graph = kb::render::MakeDefaultRenderMaterialGraphDocument();
    if (!gateway_.WriteNewMaterial(path, material)) {
        console_.Error("Materials", "Material asset could not be written: " + path.generic_string());
        return false;
    }

    console_.Info("Materials", "Material asset created: " + path.generic_string());
    return true;
}

bool EditorMaterialAssetAuthoring::CreateInstance(kb::assets::AssetId parentMaterial) {
    const kb::assets::AssetMetadata* parent = gateway_.Scene().Assets().Manager().Registry().Find(parentMaterial);
    if (parent == nullptr || parent->type != "RenderMaterial") {
        console_.Error("Materials", "Material instance creation requires a parent material asset.");
        return false;
    }

    std::filesystem::path parentPath = parent->physicalPath;
    if (parentPath.empty()) {
        const std::optional<std::filesystem::path> resolved = gateway_.Scene().Assets().Manager().Mounts().Resolve(parent->virtualPath);
        if (!resolved.has_value()) {
            console_.Error("Materials", "Parent material path could not be resolved for material instance creation.");
            return false;
        }
        parentPath = *resolved;
    }

    kb::render::RenderMaterialInstanceAssetData instance{};
    instance.parentMaterialAssetId = parentMaterial;
    const std::filesystem::path baseName = parentPath.stem().string() + std::string{ "Instance" };
    const std::filesystem::path path = EditorMaterialAssetGateway::UniqueFilePath(parentPath.parent_path(), baseName.string(), ".kbmatinst");
    if (!gateway_.WriteNewMaterialInstance(path, instance)) {
        console_.Error("Materials", "Material instance asset could not be written: " + path.generic_string());
        return false;
    }

    console_.Info("Materials", "Material instance asset created: " + path.generic_string());
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

bool EditorMaterialAssetAuthoring::SetTextureAsset(kb::assets::AssetId id, EditorMaterialTextureSlot slot, kb::assets::AssetId textureId) {
    if (textureId.IsValid()) {
        const kb::assets::AssetMetadata* texture = gateway_.Scene().Assets().Manager().Registry().Find(textureId);
        if (texture == nullptr || !IsTextureAsset(*texture)) {
            console_.Error("Materials", "Material texture slot rejected a non-texture asset.");
            return false;
        }
    }
    const bool saved = gateway_.Mutate(id, [slot, textureId](kb::render::RenderMaterialAssetData& asset) {
        TextureSlot(asset, slot) = textureId.value;
    });
    if (!saved) {
        console_.Error("Materials", "Material texture slot could not be saved.");
    }
    return saved;
}

bool EditorMaterialAssetAuthoring::CycleTextureAsset(kb::assets::AssetId id, EditorMaterialTextureSlot slot) {
    const std::optional<kb::render::RenderMaterialAssetData> material = Read(id);
    if (!material.has_value()) {
        console_.Error("Materials", "Material texture slot could not be read.");
        return false;
    }

    std::vector<kb::assets::AssetId> textures;
    for (const kb::assets::AssetMetadata& metadata : gateway_.Scene().Assets().Manager().Registry().All()) {
        if (IsTextureAsset(metadata)) {
            textures.push_back(metadata.id);
        }
    }
    std::ranges::sort(textures, [](kb::assets::AssetId lhs, kb::assets::AssetId rhs) {
        return lhs.value < rhs.value;
    });

    const std::uint64_t current = TextureSlotValue(*material, slot);
    if (textures.empty()) {
        return SetTextureAsset(id, slot, {});
    }
    if (current == 0U) {
        return SetTextureAsset(id, slot, textures.front());
    }
    auto currentIt = std::ranges::find_if(textures, [current](kb::assets::AssetId candidate) {
        return candidate.value == current;
    });
    if (currentIt == textures.end()) {
        return SetTextureAsset(id, slot, {});
    }
    ++currentIt;
    return currentIt == textures.end() ? SetTextureAsset(id, slot, {}) : SetTextureAsset(id, slot, *currentIt);
}

} // namespace kb::editor
