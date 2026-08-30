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
    // True when the authoritative external source graph was unresolvable and the
    // graph embedded in the .kbmat was returned as a fail-safe fallback instead
    // (Finding 1: avoids turning every mesh magenta when a .kbmatgraph is
    // renamed/deleted). Never set when the external graph resolves — the external
    // always wins there, so a *changed* external graph is still authoritative.
    bool usedInlineFallback = false;
};

struct RuntimeMaterialFunctionLibraryBuildResult {
    RenderMaterialGraphFunctionLibrary library;
    std::vector<RenderMaterialGraphDiagnostic> diagnostics;
};

[[nodiscard]] std::filesystem::path ResolveRuntimeMaterialAssetPhysicalPath(
    const kb::assets::AssetManager& manager,
    const kb::assets::AssetMetadata& metadata);

[[nodiscard]] RuntimeMaterialSourceGraphLoadResult LoadRuntimeMaterialSourceGraph(
    kb::assets::AssetManager& manager,
    const RenderMaterialAssetData& material);

[[nodiscard]] RuntimeMaterialFunctionLibraryBuildResult BuildRuntimeMaterialFunctionLibrary(
    kb::assets::AssetManager& manager,
    const RenderMaterialGraphDocument& graph);

[[nodiscard]] std::vector<RenderMaterialGraphDiagnostic> LoadRuntimeMaterialParameterCollectionDefaults(
    kb::assets::AssetManager& manager,
    const RenderMaterialGraphDocument& graph);

[[nodiscard]] std::string RuntimeMaterialParseDiagnosticMessage(
    const RenderMaterialAssetParseDiagnostic& diagnostic);

} // namespace kb::render
