#include "scene/material/EditorEmbeddedMaterialExtractor.hpp"

#include "assets/EditorAssetBrowserState.hpp"
#include "console/EditorConsoleState.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMeshAssetLoader.hpp"
#include "scene/material/EditorEmbeddedMaterialAssetWriter.hpp"

#include <memory>
#include <optional>

namespace kb::editor {
namespace {

[[nodiscard]] bool IsMeshAsset(const kb::assets::AssetMetadata& metadata) noexcept {
    return metadata.type == "RenderMesh" || metadata.importCategory == "Mesh";
}

} // namespace

EditorEmbeddedMaterialExtractor::EditorEmbeddedMaterialExtractor(kb::scene::Scene& scene, EditorAssetBrowserState& browser, EditorConsoleState& console) noexcept
    : scene_(scene)
    , browser_(browser)
    , console_(console) {}

std::filesystem::path EditorEmbeddedMaterialExtractor::OutputFolderFor(const std::filesystem::path& meshVirtualPath) const {
    const std::filesystem::path parent = meshVirtualPath.parent_path();
    return parent.empty() ? std::filesystem::path{ "/Game" } : parent;
}

EditorEmbeddedMaterialExtractionResult EditorEmbeddedMaterialExtractor::Extract(kb::assets::AssetId meshAssetId) {
    EditorEmbeddedMaterialExtractionResult result{};
    kb::assets::AssetManager& manager = scene_.Assets().Manager();
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMeshAssetLoader>()));
    static_cast<void>(manager.RegisterLoader(std::make_unique<kb::render::RenderMaterialAssetLoader>()));

    const kb::assets::AssetMetadata* meshMetadata = manager.Registry().Find(meshAssetId);
    if (meshMetadata == nullptr || !IsMeshAsset(*meshMetadata)) {
        result.diagnostics.push_back("Selected asset is not a render mesh.");
        console_.Error("Materials", result.diagnostics.back());
        return result;
    }
    const kb::assets::AssetMetadata meshMetadataCopy = *meshMetadata;

    const kb::assets::AssetHandle<kb::render::RenderMeshAssetData> mesh = manager.Load<kb::render::RenderMeshAssetData>(meshAssetId);
    if (!mesh.IsLoaded()) {
        result.diagnostics.push_back("Mesh asset could not be loaded for embedded material extraction.");
        console_.Error("Materials", result.diagnostics.back());
        return result;
    }
    if (mesh->embeddedMaterials.empty()) {
        result.diagnostics.push_back("Mesh asset has no embedded materials.");
        console_.Warning("Materials", result.diagnostics.back());
        return result;
    }

    const std::filesystem::path outputVirtualFolder = OutputFolderFor(meshMetadataCopy.virtualPath);
    const std::optional<std::filesystem::path> outputFolder = manager.Mounts().Resolve(outputVirtualFolder / "probe");
    if (!outputFolder.has_value()) {
        result.diagnostics.push_back("Could not resolve material output folder: " + outputVirtualFolder.generic_string());
        console_.Error("Materials", result.diagnostics.back());
        return result;
    }

    const std::filesystem::path physicalFolder = outputFolder->parent_path();
    for (std::uint32_t slotIndex = 0U; slotIndex < mesh->embeddedMaterials.size(); ++slotIndex) {
        const std::optional<EditorExtractedMaterialSlot> extracted = EditorEmbeddedMaterialAssetWriter::Write(
            mesh->embeddedMaterials[slotIndex],
            slotIndex,
            meshMetadataCopy,
            physicalFolder,
            manager,
            result.diagnostics);
        if (extracted.has_value()) {
            result.slots.push_back(*extracted);
        }
    }

    if (result.slots.empty()) {
        console_.Error("Materials", "No embedded materials were extracted.");
        return result;
    }

    if (const EditorExtractedMaterialSlot& last = result.slots.back(); last.materialAssetId.IsValid()) {
        static_cast<void>(browser_.SelectAsset(last.materialAssetId, manager));
    }

    console_.Info("Materials", "Extracted " + std::to_string(result.slots.size()) + " embedded material asset(s).");
    for (const std::string& diagnostic : result.diagnostics) {
        console_.Warning("Materials", diagnostic);
    }
    return result;
}

} // namespace kb::editor
