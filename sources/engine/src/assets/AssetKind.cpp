#include "engine/assets/AssetKind.hpp"

#include "engine/assets/AssetMetadata.hpp"

namespace kb::assets {
namespace {

// The one place the kind <-> AssetMetadata::type mapping lives. The
// "Render*" strings intentionally duplicate the literals kb_render's loaders
// return (RenderMeshAssetLoader::Type() etc.) rather than #include-ing them,
// exactly as ScriptMeshRendererApi/ScriptMaterialInstanceApi already do — a
// kind check must stay a zero-dependency string compare so it works in a
// headless kb_engine build with no renderer linked.
[[nodiscard]] bool TypeIsMaterial(std::string_view type) noexcept {
    // Runtime-referenceable material kinds only: a base material and a
    // material instance. The authoring-only sub-types (RenderMaterialType/
    // Graph/Function) are deliberately NOT part of the runtime "Material"
    // kind — they are never a gameplay material reference — matching
    // ScriptMeshRendererApi::IsRenderMaterialAsset.
    return type == "RenderMaterial" || type == "RenderMaterialInstance";
}

[[nodiscard]] bool MetadataIsAudio(const AssetMetadata& metadata) noexcept {
    return metadata.type == "AudioClip" || (metadata.type == "ImportedAsset" && metadata.importCategory == "Audio");
}

} // namespace

std::string_view ToString(AssetKind kind) noexcept {
    switch (kind) {
    case AssetKind::Mesh:
        return "Mesh";
    case AssetKind::Material:
        return "Material";
    case AssetKind::Texture:
        return "Texture";
    case AssetKind::Audio:
        return "Audio";
    case AssetKind::Prefab:
        return "Prefab";
    case AssetKind::Scene:
        return "Scene";
    case AssetKind::Graph:
        return "Graph";
    case AssetKind::InputAction:
        return "InputAction";
    case AssetKind::InputMap:
        return "InputMap";
    }
    return "";
}

bool TryParseAssetKind(std::string_view name, AssetKind& out) noexcept {
    for (std::uint8_t index = 0; index < static_cast<std::uint8_t>(kAssetKindCount); ++index) {
        const AssetKind kind = static_cast<AssetKind>(index);
        if (ToString(kind) == name) {
            out = kind;
            return true;
        }
    }
    return false;
}

bool AssetMatchesKind(const AssetMetadata& metadata, AssetKind kind) noexcept {
    switch (kind) {
    case AssetKind::Mesh:
        return metadata.type == "RenderMesh";
    case AssetKind::Material:
        return TypeIsMaterial(metadata.type);
    case AssetKind::Texture:
        return metadata.type == "RenderTexture";
    case AssetKind::Audio:
        return MetadataIsAudio(metadata);
    case AssetKind::Prefab:
        return metadata.type == "ScenePrefab";
    case AssetKind::Scene:
        return metadata.type == "Scene";
    case AssetKind::Graph:
        return metadata.type == "VisualGraph";
    case AssetKind::InputAction:
        return metadata.type == "InputAction";
    case AssetKind::InputMap:
        return metadata.type == "InputMappingContext";
    }
    return false;
}

bool TryClassifyAssetKind(const AssetMetadata& metadata, AssetKind& out) noexcept {
    for (std::uint8_t index = 0; index < static_cast<std::uint8_t>(kAssetKindCount); ++index) {
        const AssetKind kind = static_cast<AssetKind>(index);
        if (AssetMatchesKind(metadata, kind)) {
            out = kind;
            return true;
        }
    }
    return false;
}

} // namespace kb::assets
