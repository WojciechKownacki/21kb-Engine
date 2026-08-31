#include "scene/assets/SceneAssetLoader.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/SceneAssetMeta.hpp"
#include "engine/scene/SceneDocument.hpp"
#include "scene/asset/io/SceneAssetFormat.hpp"
#include "scene/asset/io/SceneAssetMetaReader.hpp"
#include "scene/asset/io/SceneAssetReader.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace kb::scene {
namespace {

// The asset registry has exactly one identifier scheme:
// MakeAssetId(NormalizeAssetPath(virtualPath) + ":" + type). It is applied by
// AssetDiscoveryService, AssetManager::RegisterAsset and AssetImportService alike,
// so every id this loader reports must be in that scheme - an id in any other
// scheme resolves to nothing and silently drops the asset out of the dependency
// graph the cooker walks.
constexpr std::string_view kNestedPrefabRole = "nestedPrefab";

void AppendUnique(std::vector<kb::assets::AssetId>& dependencies, kb::assets::AssetId id) {
    if (!id.IsValid()) {
        return;
    }
    const bool known = std::ranges::any_of(dependencies, [id](kb::assets::AssetId existing) noexcept {
        return existing.value == id.value;
    });
    if (!known) {
        dependencies.push_back(id);
    }
}

struct SceneDependencyResolution {
    std::vector<kb::assets::AssetId> dependencies;
    std::optional<std::string> diagnostic;
};

[[nodiscard]] SceneDependencyResolution ResolveSceneDependencies(
    const SceneAssetMeta& meta,
    const kb::assets::AssetRegistry& registry,
    ScenePrefabGuidAssetIndex& guidIndex) {
    SceneDependencyResolution result;
    result.dependencies.reserve(meta.dependencies.size());
    for (const SceneAssetDependency& dependency : meta.dependencies) {
        if (dependency.role != kNestedPrefabRole) {
            AppendUnique(result.dependencies, dependency.assetId);
            continue;
        }

        const ScenePrefabGuidClaim* claim = guidIndex.Find(registry, dependency.assetId);
        if (claim == nullptr) {
            if (!result.diagnostic.has_value()) {
                result.diagnostic = "nests prefab guid reference " +
                    std::to_string(dependency.assetId.value) +
                    " which no registered ScenePrefab asset declares";
            }
            continue;
        }
        if (claim->IsContested() && !result.diagnostic.has_value()) {
            result.diagnostic = "nests a prefab whose guid is declared by " +
                std::to_string(claim->claimCount) + " prefab assets (" +
                claim->firstVirtualPath + ", " + claim->secondVirtualPath +
                "): a copied \".kbprefab\" keeps the guid of its original, so the reference names no single "
                "file and cannot be resolved deterministically";
        }
        if (!claim->IsContested()) {
            AppendUnique(result.dependencies, claim->assetId);
        }
    }
    return result;
}

[[nodiscard]] std::vector<std::uint64_t> SortedDependencyIds(
    const std::vector<kb::assets::AssetId>& dependencies) {
    std::vector<std::uint64_t> ids;
    ids.reserve(dependencies.size());
    for (const kb::assets::AssetId dependency : dependencies) {
        ids.push_back(dependency.value);
    }
    std::ranges::sort(ids);
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

} // namespace

std::string_view SceneAssetLoader::Type() const noexcept {
    return "Scene";
}

std::type_index SceneAssetLoader::PayloadType() const noexcept {
    return typeid(SceneDocument);
}

std::vector<std::string> SceneAssetLoader::Extensions() const {
    return {std::string{SceneAssetFormat::Extension}};
}

kb::assets::AssetLoadResult SceneAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    SceneDocumentLoadResult loaded;
    if (request.IsPackaged()) {
        std::vector<std::uint8_t> sourceBytes;
        std::string error;
        if (!request.ReadSourceBytes(sourceBytes, error)) {
            return kb::assets::AssetLoadResult{.asset = {}, .error = std::move(error)};
        }
        loaded = SceneAssetReader::Read(std::move(sourceBytes));
    } else {
        // Loose scenes retain their .meta integrity check. Packaged payloads are
        // authenticated by RuntimeAssetPack and intentionally have no sidecar.
        loaded = SceneAssetReader::Read(request.resolvedPath);
    }
    if (!loaded.succeeded) {
        return kb::assets::AssetLoadResult{.asset = {}, .error = std::move(loaded.error)};
    }

    return kb::assets::AssetLoadResult{.asset = std::make_shared<SceneDocument>(std::move(loaded.document)), .error = {}};
}

std::vector<kb::assets::AssetId> SceneAssetLoader::DiscoverDependencies(
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetRegistry& registry) const {
    // The scene's references live in its ".meta" sidecar, which SceneAssetWriter
    // rewrites on every save. Reading it is what makes a scene a node in the asset
    // dependency graph at all; parsing the scene document itself would cost a full
    // node/component decode per discovery pass for the same answer.
    const SceneAssetMetaReadResult meta = SceneAssetMetaReader::Read(SceneAssetMetaPath(metadata.physicalPath));
    if (!meta.succeeded) {
        // Discovery cannot return a diagnostic, so leave the dependency list empty
        // and let ValidateDependencies reject the asset. Treating an unreadable
        // sidecar as a dependency-free scene would let a cooker publish an incomplete
        // closure before SceneAssetReader ever gets a chance to verify the scene.
        return {};
    }

    return ResolveSceneDependencies(meta.meta, registry, nestedPrefabGuidIndex_).dependencies;
}

std::optional<std::string> SceneAssetLoader::ValidateDependencies(
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetRegistry& registry) const {
    const std::filesystem::path metaPath = SceneAssetMetaPath(metadata.physicalPath);
    const SceneAssetMetaReadResult meta = SceneAssetMetaReader::Read(metaPath);
    if (!meta.succeeded) {
        return "has no readable scene metadata sidecar \"" + metaPath.generic_string() + "\": " + meta.error;
    }
    const SceneDocumentLoadResult scene = SceneAssetReader::Read(metadata.physicalPath);
    if (!scene.succeeded) {
        return "does not match scene metadata sidecar \"" + metaPath.generic_string() + "\": " + scene.error;
    }
    if (metadata.contentHash != 0U && meta.meta.contentHashFnv1a64 != metadata.contentHash) {
        return "changed after asset discovery; retry before cooking";
    }

    // Deliberately a local index rather than the discovery-pass cache: this runs on
    // whichever thread asked for the scene, while the cached one belongs to the
    // discovery thread. It is only built when the scene actually nests a prefab.
    ScenePrefabGuidAssetIndex guidIndex;
    SceneDependencyResolution current = ResolveSceneDependencies(meta.meta, registry, guidIndex);
    if (current.diagnostic.has_value()) {
        return current.diagnostic;
    }
    if (SortedDependencyIds(current.dependencies) != SortedDependencyIds(metadata.dependencies)) {
        return "dependency metadata changed after asset discovery; retry before cooking";
    }
    return std::nullopt;
}

std::optional<std::string> SceneAssetLoader::ValidateRuntimeDependencies(
    const kb::assets::AssetLoadRequest& request,
    const kb::assets::AssetRegistry& registry) const {
    // A runtime pack authenticates the scene payload and carries the dependency
    // closure in its manifest; authored .meta sidecars intentionally are not
    // deployed. Loose project assets still require their writer-produced sidecar.
    if (request.IsPackaged()) {
        return std::nullopt;
    }
    return ValidateDependencies(request.metadata, registry);
}

} // namespace kb::scene
