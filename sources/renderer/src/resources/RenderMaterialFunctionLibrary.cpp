#include "kb/render/resources/RenderMaterialFunctionLibrary.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "kb/render/resources/RenderMaterialFunctionAssetLoader.hpp"

#include <algorithm>
#include <filesystem>
#include <unordered_set>
#include <utility>

namespace kb::render {
namespace {

void AppendDiagnostic(RenderMaterialFunctionLibraryBuildResult& result, std::uint64_t assetId, const std::string& path, std::string message) {
    result.diagnostics.push_back(RenderMaterialGraphDiagnostic{
        .severity = RenderMaterialGraphDiagnosticSeverity::Error,
        .kind = RenderMaterialGraphDiagnosticKind::MissingMaterialFunction,
        .assetId = assetId,
        .sourcePath = path,
        .message = std::move(message),
    });
}

} // namespace

RenderMaterialFunctionLibraryBuildResult BuildRenderMaterialFunctionLibrary(
    const kb::assets::AssetRegistry& registry,
    const RenderMaterialGraphDocument& graph,
    const RenderMaterialFunctionGraphReader& reader) {
    RenderMaterialFunctionLibraryBuildResult result{};
    std::vector<std::uint64_t> pending = DiscoverRenderMaterialGraphFunctionDependencies(graph);
    std::unordered_set<std::uint64_t> visited;
    while (!pending.empty()) {
        const std::uint64_t assetId = pending.back();
        pending.pop_back();
        if (assetId == 0U || !visited.insert(assetId).second) continue;

        const kb::assets::AssetMetadata* metadata = registry.Find(kb::assets::AssetId{ assetId });
        if (metadata == nullptr || metadata->type != kRenderMaterialFunctionAssetType) {
            AppendDiagnostic(result, assetId, {}, "Material function asset " + std::to_string(assetId) + " is missing or has the wrong asset type.");
            continue;
        }
        std::string error;
        std::optional<RenderMaterialGraphDocument> function = reader(*metadata, error);
        if (!function.has_value()) {
            AppendDiagnostic(result, assetId, metadata->virtualPath.generic_string(),
                error.empty() ? "Material function asset " + std::to_string(assetId) + " could not be loaded."
                              : "Material function asset could not be loaded: " + error);
            continue;
        }
        for (const std::uint64_t nested : DiscoverRenderMaterialGraphFunctionDependencies(*function)) {
            if (!visited.contains(nested)) pending.push_back(nested);
        }
        result.library.entries.push_back(RenderMaterialGraphFunctionLibraryEntry{
            .assetId = metadata->id.value,
            .contentHash = metadata->contentHash,
            .name = metadata->virtualPath.generic_string(),
            .graph = std::move(*function),
        });
    }
    return result;
}

RenderMaterialFunctionLibraryBuildResult BuildRenderMaterialFunctionLibrary(
    kb::assets::AssetManager& manager,
    const RenderMaterialGraphDocument& graph) {
    return BuildRenderMaterialFunctionLibrary(manager.Registry(), graph,
        [&manager](const kb::assets::AssetMetadata& metadata, std::string& error) -> std::optional<RenderMaterialGraphDocument> {
            const kb::assets::AssetHandle<RenderMaterialFunctionAssetData> loaded = manager.Load<RenderMaterialFunctionAssetData>(metadata.id);
            if (!loaded.IsLoaded()) {
                error = manager.LastError();
                return std::nullopt;
            }
            return loaded->graph;
        });
}

RenderMaterialFunctionLibraryBuildResult BuildRenderMaterialFunctionLibraryFromFiles(
    const kb::assets::AssetRegistry& registry,
    const RenderMaterialGraphDocument& graph) {
    return BuildRenderMaterialFunctionLibrary(registry, graph,
        [](const kb::assets::AssetMetadata& metadata, std::string& error) -> std::optional<RenderMaterialGraphDocument> {
            const RenderMaterialAssetParseResult loaded = RenderMaterialFunctionAssetLoader::LoadFunctionWithDiagnostics(metadata.physicalPath, metadata.id);
            if (!loaded.asset.has_value()) {
                error = loaded.ErrorMessage();
                return std::nullopt;
            }
            return loaded.asset->graph;
        });
}

RenderMaterialFunctionLibraryBuildResult BuildRenderMaterialFunctionLibraryFromFiles(
    const kb::assets::AssetManager& manager,
    const RenderMaterialGraphDocument& graph) {
    return BuildRenderMaterialFunctionLibrary(manager.Registry(), graph,
        [&manager](const kb::assets::AssetMetadata& metadata, std::string& error) -> std::optional<RenderMaterialGraphDocument> {
            std::filesystem::path path = metadata.physicalPath;
            if (path.empty()) path = manager.Mounts().Resolve(metadata.virtualPath).value_or(std::filesystem::path{});
            const RenderMaterialAssetParseResult loaded = RenderMaterialFunctionAssetLoader::LoadFunctionWithDiagnostics(path, metadata.id);
            if (!loaded.asset.has_value()) {
                error = loaded.ErrorMessage();
                return std::nullopt;
            }
            return loaded.asset->graph;
        });
}

std::uint64_t HashRenderMaterialFunctionLibrary(const RenderMaterialGraphFunctionLibrary& library) noexcept {
    std::vector<std::pair<std::uint64_t, std::uint64_t>> parts;
    parts.reserve(library.entries.size());
    for (const RenderMaterialGraphFunctionLibraryEntry& entry : library.entries) parts.emplace_back(entry.assetId, entry.contentHash);
    std::sort(parts.begin(), parts.end());
    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            hash ^= (value >> shift) & 0xFFU;
            hash *= 1099511628211ULL;
        }
    };
    for (const auto& [assetId, contentHash] : parts) {
        mix(assetId);
        mix(contentHash);
    }
    return hash == 0U ? 1U : hash;
}

} // namespace kb::render
