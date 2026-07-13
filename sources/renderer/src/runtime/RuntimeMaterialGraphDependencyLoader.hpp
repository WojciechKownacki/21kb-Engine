#pragma once

#include "engine/assets/AssetId.hpp"
#include "kb/render/resources/RenderMaterialAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphDocument.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kb::assets {
class AssetManager;
struct AssetMetadata;
}

namespace kb::render {

struct RuntimeMaterialSourceGraphLoadResult {
    std::optional<RenderMaterialGraphDocument> graph;
    RenderMaterialAssetParseResult parseResult;
    kb::assets::AssetId assetId{};
    std::filesystem::path path;
};

struct RuntimeMaterialFunctionLibraryBuildResult {
    RenderMaterialGraphFunctionLibrary library;
    std::vector<RenderMaterialGraphDiagnostic> diagnostics;
};

[[nodiscard]] std::filesystem::path ResolveRuntimeMaterialAssetPhysicalPath(
    const kb::assets::AssetManager& manager,
    const kb::assets::AssetMetadata& metadata);

[[nodiscard]] RuntimeMaterialSourceGraphLoadResult LoadRuntimeMaterialSourceGraph(
    const kb::assets::AssetManager& manager,
    const RenderMaterialAssetData& material);

[[nodiscard]] RuntimeMaterialFunctionLibraryBuildResult BuildRuntimeMaterialFunctionLibrary(
    const kb::assets::AssetManager& manager,
    const RenderMaterialGraphDocument& graph);

[[nodiscard]] std::vector<RenderMaterialGraphDiagnostic> LoadRuntimeMaterialParameterCollectionDefaults(
    const kb::assets::AssetManager& manager,
    const RenderMaterialGraphDocument& graph);

[[nodiscard]] std::string RuntimeMaterialParseDiagnosticMessage(
    const RenderMaterialAssetParseDiagnostic& diagnostic);

} // namespace kb::render
