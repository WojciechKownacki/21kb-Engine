#include "scene/material/EditorEmbeddedMaterialTextureResolver.hpp"

#include "engine/assets/AssetManager.hpp"

#include <array>
#include <optional>
#include <string_view>

namespace kb::editor {
namespace {

[[nodiscard]] bool IsTextureAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderTexture" || metadata.type == "Texture" || metadata.importCategory == "Texture";
}

[[nodiscard]] std::string Normalize(const std::filesystem::path& path) {
    return kb::assets::NormalizeAssetPath(path);
}

[[nodiscard]] std::filesystem::path MeshVirtualFolder(const kb::assets::AssetMetadata& meshMetadata) {
    const std::filesystem::path parent = meshMetadata.virtualPath.parent_path();
    return parent.empty() ? std::filesystem::path{ "/Game" } : parent;
}

[[nodiscard]] std::vector<std::filesystem::path> CandidateTexturePaths(
    const std::filesystem::path& texturePath,
    const kb::assets::AssetMetadata& meshMetadata) {
    std::vector<std::filesystem::path> candidates;
    if (texturePath.empty()) {
        return candidates;
    }

    if (texturePath.is_absolute()) {
        candidates.push_back(texturePath.lexically_normal());
    } else {
        candidates.push_back((MeshVirtualFolder(meshMetadata) / texturePath).lexically_normal());
        candidates.push_back((std::filesystem::path{ "/Game" } / texturePath).lexically_normal());
    }
    return candidates;
}

[[nodiscard]] std::optional<kb::assets::AssetId> FindTextureByPath(
    const std::filesystem::path& texturePath,
    const kb::assets::AssetMetadata& meshMetadata,
    const kb::assets::AssetManager& manager) {
    for (const std::filesystem::path& candidate : CandidateTexturePaths(texturePath, meshMetadata)) {
        if (const kb::assets::AssetMetadata* metadata = manager.Registry().FindByPath(candidate);
            metadata != nullptr && IsTextureAsset(*metadata)) {
            return metadata->id;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<kb::assets::AssetId> FindTextureByFilename(
    const std::filesystem::path& texturePath,
    const kb::assets::AssetManager& manager) {
    const std::string filename = Normalize(texturePath.filename());
    if (filename.empty()) {
        return std::nullopt;
    }

    std::optional<kb::assets::AssetId> matched;
    for (const kb::assets::AssetMetadata& metadata : manager.Registry().All()) {
        if (!IsTextureAsset(metadata) || Normalize(metadata.virtualPath.filename()) != filename) {
            continue;
        }
        if (matched.has_value()) {
            return std::nullopt;
        }
        matched = metadata.id;
    }
    return matched;
}

[[nodiscard]] std::optional<kb::assets::AssetId> ResolveTexture(
    const std::filesystem::path& texturePath,
    const kb::assets::AssetMetadata& meshMetadata,
    const kb::assets::AssetManager& manager) {
    if (texturePath.empty()) {
        return std::nullopt;
    }
    if (const std::optional<kb::assets::AssetId> byPath = FindTextureByPath(texturePath, meshMetadata, manager)) {
        return byPath;
    }
    return FindTextureByFilename(texturePath, manager);
}

struct TextureSlotBinding {
    std::string_view label;
    const std::string* sourcePath = nullptr;
    std::string* materialPath = nullptr;
    std::uint64_t* materialAssetId = nullptr;
};

void ResolveSlot(
    const TextureSlotBinding& binding,
    const kb::assets::AssetMetadata& meshMetadata,
    const kb::assets::AssetManager& manager,
    std::vector<std::string>& diagnostics) {
    if (binding.sourcePath == nullptr || binding.materialPath == nullptr || binding.materialAssetId == nullptr || binding.sourcePath->empty()) {
        return;
    }

    *binding.materialPath = *binding.sourcePath;
    if (const std::optional<kb::assets::AssetId> textureId = ResolveTexture(*binding.sourcePath, meshMetadata, manager)) {
        *binding.materialAssetId = textureId->value;
        binding.materialPath->clear();
        return;
    }

    diagnostics.push_back(
        "Unresolved embedded material texture '" + *binding.sourcePath + "' for " + std::string{ binding.label } + ".");
}

} // namespace

void EditorEmbeddedMaterialTextureResolver::Resolve(
    kb::render::RenderMaterialAssetData& material,
    const kb::render::RenderMeshEmbeddedMaterial& embedded,
    const kb::assets::AssetMetadata& meshMetadata,
    const kb::assets::AssetManager& manager,
    std::vector<std::string>& diagnostics) {
    std::array<TextureSlotBinding, 13U> slots{
        TextureSlotBinding{ "albedo", &embedded.albedoTexturePath, &material.albedoTexturePath, &material.desc.albedoTextureAssetId },
        TextureSlotBinding{ "normal", &embedded.normalTexturePath, &material.normalTexturePath, &material.desc.normalTextureAssetId },
        TextureSlotBinding{ "metallic-roughness", &embedded.metallicRoughnessTexturePath, &material.metallicRoughnessTexturePath, &material.desc.metallicRoughnessTextureAssetId },
        TextureSlotBinding{ "occlusion", &embedded.occlusionTexturePath, &material.occlusionTexturePath, &material.desc.occlusionTextureAssetId },
        TextureSlotBinding{ "emissive", &embedded.emissiveTexturePath, &material.emissiveTexturePath, &material.desc.emissiveTextureAssetId },
        TextureSlotBinding{ "clearcoat", &embedded.clearcoatTexturePath, &material.clearcoatTexturePath, &material.desc.clearcoatTextureAssetId },
        TextureSlotBinding{ "clearcoat roughness", &embedded.clearcoatRoughnessTexturePath, &material.clearcoatRoughnessTexturePath, &material.desc.clearcoatRoughnessTextureAssetId },
        TextureSlotBinding{ "sheen color", &embedded.sheenColorTexturePath, &material.sheenColorTexturePath, &material.desc.sheenColorTextureAssetId },
        TextureSlotBinding{ "transmission", &embedded.transmissionTexturePath, &material.transmissionTexturePath, &material.desc.transmissionTextureAssetId },
        TextureSlotBinding{ "thickness", &embedded.thicknessTexturePath, &material.thicknessTexturePath, &material.desc.thicknessTextureAssetId },
        TextureSlotBinding{ "anisotropy", &embedded.anisotropyTexturePath, &material.anisotropyTexturePath, &material.desc.anisotropyTextureAssetId },
        TextureSlotBinding{ "decal", &embedded.decalTexturePath, &material.decalTexturePath, &material.desc.decalTextureAssetId },
        TextureSlotBinding{ "layer mask", &embedded.layerMaskTexturePath, &material.layerMaskTexturePath, &material.desc.layerMaskTextureAssetId },
    };

    for (const TextureSlotBinding& slot : slots) {
        ResolveSlot(slot, meshMetadata, manager, diagnostics);
    }
}

} // namespace kb::editor
