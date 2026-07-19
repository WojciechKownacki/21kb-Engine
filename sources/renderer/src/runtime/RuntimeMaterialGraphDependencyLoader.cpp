#include "runtime/RuntimeMaterialGraphDependencyLoader.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "kb/render/resources/RenderMaterialFunctionAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialGraphAssetLoader.hpp"
#include "kb/render/resources/RenderMaterialParameterCollection.hpp"

#include <string>
#include <unordered_set>
#include <utility>

namespace kb::render {
namespace {

[[nodiscard]] const kb::assets::AssetMetadata* ResolveMaterialSourceGraphMetadata(
    const kb::assets::AssetManager& manager,
    const RenderMaterialAssetData& material) {
    const kb::assets::AssetMetadata* metadata = material.graphSourceAssetId != 0U
        ? manager.Registry().Find(kb::assets::AssetId{ material.graphSourceAssetId })
        : nullptr;
    if ((metadata == nullptr || metadata->type != kRenderMaterialGraphAssetType) &&
        !material.graphSourceAssetPath.empty()) {
        metadata = manager.Registry().FindByPath(std::filesystem::path{ material.graphSourceAssetPath });
    }
    return metadata != nullptr && metadata->type == kRenderMaterialGraphAssetType ? metadata : nullptr;
}

void AppendRuntimeFunctionLibraryDiagnostic(
    RuntimeMaterialFunctionLibraryBuildResult& result,
    std::uint64_t assetId,
    std::filesystem::path path,
    std::string message) {
    result.diagnostics.push_back(RenderMaterialGraphDiagnostic{
        .severity = RenderMaterialGraphDiagnosticSeverity::Error,
        .kind = RenderMaterialGraphDiagnosticKind::MissingMaterialFunction,
        .assetId = assetId,
        .sourcePath = path.generic_string(),
        .message = std::move(message),
    });
}

void AppendRuntimeMaterialParameterCollectionDiagnostic(
    std::vector<RenderMaterialGraphDiagnostic>& diagnostics,
    std::uint64_t assetId,
    std::filesystem::path path,
    std::string message) {
    diagnostics.push_back(RenderMaterialGraphDiagnostic{
        .severity = RenderMaterialGraphDiagnosticSeverity::Error,
        .kind = RenderMaterialGraphDiagnosticKind::ShaderGenerationFailed,
        .assetId = assetId,
        .sourcePath = path.generic_string(),
        .message = std::move(message),
    });
}

} // namespace

std::filesystem::path ResolveRuntimeMaterialAssetPhysicalPath(
    const kb::assets::AssetManager& manager,
    const kb::assets::AssetMetadata& metadata) {
    if (!metadata.physicalPath.empty()) {
        return metadata.physicalPath;
    }
    return manager.Mounts().Resolve(metadata.virtualPath).value_or(std::filesystem::path{});
}

std::string RuntimeMaterialParseDiagnosticMessage(const RenderMaterialAssetParseDiagnostic& diagnostic) {
    std::string message{ RenderMaterialAssetParseDiagnosticCodeName(diagnostic.code) };
    if (diagnostic.line > 0U) {
        message += " line ";
        message += std::to_string(diagnostic.line);
    }
    if (!diagnostic.message.empty()) {
        message += ": ";
        message += diagnostic.message;
    }
    if (!diagnostic.text.empty()) {
        message += " [";
        message += diagnostic.text;
        message += ']';
    }
    return message;
}

RuntimeMaterialSourceGraphLoadResult LoadRuntimeMaterialSourceGraph(
    const kb::assets::AssetManager& manager,
    const RenderMaterialAssetData& material) {
    if (material.graphSourceAssetId == 0U && material.graphSourceAssetPath.empty()) {
        return RuntimeMaterialSourceGraphLoadResult{ .graph = material.graph };
    }
    const kb::assets::AssetMetadata* metadata = ResolveMaterialSourceGraphMetadata(manager, material);
    if (metadata == nullptr) {
        // Finding 1 (fail-safe fallback): the authoritative external source graph is no
        // longer registered (renamed / deleted / re-imported). Rather than resolving to
        // the error material and turning every mesh using it magenta, fall back to the
        // complete graph still embedded in the .kbmat so the material keeps rendering,
        // and surface the missing dependency as a warning. Because this branch is only
        // reached when the external graph does NOT resolve, a *changed* (still-present)
        // external graph remains authoritative — the P1.9 authoring invariant holds.
        RuntimeMaterialSourceGraphLoadResult fallback{};
        fallback.graph = material.graph;
        fallback.usedInlineFallback = true;
        fallback.path = material.graphSourceAssetPath;
        fallback.parseResult.diagnostics.push_back(RenderMaterialAssetParseDiagnostic{
            .code = RenderMaterialAssetParseDiagnosticCode::InvalidGraphField,
            .severity = RenderMaterialAssetParseDiagnosticSeverity::Warning,
            .path = material.graphSourceAssetPath,
            .message = "Authoritative source graph asset is missing; rendering the graph embedded in the material as a fallback.",
        });
        return fallback;
    }
    RuntimeMaterialSourceGraphLoadResult result{};
    result.assetId = metadata->id;
    result.path = ResolveRuntimeMaterialAssetPhysicalPath(manager, *metadata);
    result.parseResult = RenderMaterialGraphAssetLoader::LoadGraphWithDiagnostics(result.path, result.assetId);
    if (result.parseResult.asset.has_value()) {
        result.graph = result.parseResult.asset->graph;
    }
    return result;
}

RuntimeMaterialFunctionLibraryBuildResult BuildRuntimeMaterialFunctionLibrary(
    const kb::assets::AssetManager& manager,
    const RenderMaterialGraphDocument& graph) {
    RuntimeMaterialFunctionLibraryBuildResult result{};
    std::vector<std::uint64_t> pending = DiscoverRenderMaterialGraphFunctionDependencies(graph);
    std::unordered_set<std::uint64_t> visited;
    while (!pending.empty()) {
        const std::uint64_t assetId = pending.back();
        pending.pop_back();
        if (assetId == 0U || !visited.insert(assetId).second) {
            continue;
        }

        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(kb::assets::AssetId{ assetId });
        if (metadata == nullptr || metadata->type != kRenderMaterialFunctionAssetType) {
            AppendRuntimeFunctionLibraryDiagnostic(
                result, assetId, {}, "Material function asset " + std::to_string(assetId) + " is missing or has the wrong asset type.");
            continue;
        }

        const std::filesystem::path path = ResolveRuntimeMaterialAssetPhysicalPath(manager, *metadata);
        const RenderMaterialAssetParseResult loaded = RenderMaterialFunctionAssetLoader::LoadFunctionWithDiagnostics(path, metadata->id);
        if (!loaded.asset.has_value()) {
            if (loaded.diagnostics.empty()) {
                AppendRuntimeFunctionLibraryDiagnostic(
                    result, assetId, path, "Material function asset " + std::to_string(assetId) + " could not be loaded.");
            } else {
                for (const RenderMaterialAssetParseDiagnostic& diagnostic : loaded.diagnostics) {
                    AppendRuntimeFunctionLibraryDiagnostic(
                        result, assetId, diagnostic.path.empty() ? path : diagnostic.path,
                        "Material function asset could not be loaded: " + RuntimeMaterialParseDiagnosticMessage(diagnostic));
                }
            }
            continue;
        }

        result.library.entries.push_back(RenderMaterialGraphFunctionLibraryEntry{
            .assetId = metadata->id.value,
            .contentHash = metadata->contentHash,
            .name = metadata->virtualPath.generic_string(),
            .graph = loaded.asset->graph,
        });
        for (const std::uint64_t nestedAssetId : DiscoverRenderMaterialGraphFunctionDependencies(loaded.asset->graph)) {
            if (!visited.contains(nestedAssetId)) {
                pending.push_back(nestedAssetId);
            }
        }
    }
    return result;
}

std::vector<RenderMaterialGraphDiagnostic> LoadRuntimeMaterialParameterCollectionDefaults(
    const kb::assets::AssetManager& manager,
    const RenderMaterialGraphDocument& graph) {
    std::vector<RenderMaterialGraphDiagnostic> diagnostics;
    for (const std::uint64_t assetId : DiscoverRenderMaterialGraphParameterCollectionDependencies(graph)) {
        if (assetId == 0U) {
            continue;
        }
        const kb::assets::AssetMetadata* metadata = manager.Registry().Find(kb::assets::AssetId{ assetId });
        if (metadata == nullptr || metadata->type != kRenderMaterialParameterCollectionAssetType) {
            AppendRuntimeMaterialParameterCollectionDiagnostic(
                diagnostics, assetId, {}, "Material parameter collection asset " + std::to_string(assetId) + " is missing or has the wrong asset type.");
            continue;
        }

        const std::filesystem::path path = ResolveRuntimeMaterialAssetPhysicalPath(manager, *metadata);
        const RenderMaterialParameterCollectionParseResult loaded =
            RenderMaterialParameterCollectionAssetLoader::LoadCollectionWithDiagnostics(path, metadata->id);
        if (!loaded.collection.has_value()) {
            if (loaded.diagnostics.empty()) {
                AppendRuntimeMaterialParameterCollectionDiagnostic(
                    diagnostics, assetId, path, "Material parameter collection asset " + std::to_string(assetId) + " could not be loaded.");
            } else {
                for (const RenderMaterialAssetParseDiagnostic& diagnostic : loaded.diagnostics) {
                    AppendRuntimeMaterialParameterCollectionDiagnostic(
                        diagnostics, assetId, diagnostic.path.empty() ? path : diagnostic.path,
                        "Material parameter collection could not be loaded: " + RuntimeMaterialParseDiagnosticMessage(diagnostic));
                }
            }
            continue;
        }
        static_cast<void>(GlobalRenderMaterialParameterCollectionStore().LoadDefaults(metadata->id.value, *loaded.collection));
    }
    return diagnostics;
}

} // namespace kb::render
