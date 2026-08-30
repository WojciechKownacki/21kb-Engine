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
    SceneDocumentLoadResult loaded = SceneAssetReader::Read(request.resolvedPath);
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
        // A scene file with no readable sidecar declares no dependencies. This is a
        // normal state - a scene copied in by hand, or one whose sidecar has not
        // been written yet - and never a load failure: SceneAssetReader::Read still
        // rejects the scene on its own integrity check when it is actually opened.
        return {};
    }

    std::vector<kb::assets::AssetId> dependencies;
    dependencies.reserve(meta.meta.dependencies.size());
    for (const SceneAssetDependency& dependency : meta.meta.dependencies) {
        if (dependency.role != kNestedPrefabRole) {
            AppendUnique(dependencies, dependency.assetId);
            continue;
        }

        const ScenePrefabGuidClaim* claim = nestedPrefabGuidIndex_.Find(registry, dependency.assetId);
        if (claim != nullptr) {
            // An invalid id here means the guid is claimed by more than one prefab
            // file. Naming one of them would be a coin flip the prefab runtime does
            // not repeat - it binds a guid to the first file loaded - so the edge is
            // dropped and ValidateDependencies reports the collision by name rather
            // than letting the cooker package an asset the game will not resolve to.
            AppendUnique(dependencies, claim->assetId);
        }
        // A guid no registered prefab asset declares is dropped rather than reported
        // as-is: the reference is not a registry id, so emitting it would put an id
        // that resolves to nothing into the graph. This matches how the material
        // loader treats a texture path it cannot resolve.
    }
    return dependencies;
}

std::optional<std::string> SceneAssetLoader::ValidateDependencies(
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetRegistry& registry) const {
    const SceneAssetMetaReadResult meta = SceneAssetMetaReader::Read(SceneAssetMetaPath(metadata.physicalPath));
    if (!meta.succeeded) {
        return std::nullopt;
    }

    // Deliberately a local index rather than the discovery-pass cache: this runs on
    // whichever thread asked for the scene, while the cached one belongs to the
    // discovery thread. It is only built when the scene actually nests a prefab.
    ScenePrefabGuidAssetIndex guidIndex;
    for (const SceneAssetDependency& dependency : meta.meta.dependencies) {
        if (dependency.role != kNestedPrefabRole) {
            continue;
        }
        const ScenePrefabGuidClaim* claim = guidIndex.Find(registry, dependency.assetId);
        if (claim == nullptr || !claim->IsContested()) {
            continue;
        }
        return "nests a prefab whose guid is declared by " + std::to_string(claim->claimCount) +
            " prefab assets (" + claim->firstVirtualPath + ", " + claim->secondVirtualPath +
            "): a copied \".kbprefab\" keeps the guid of its original, so the reference names no single "
            "file and cannot be resolved deterministically";
    }
    return std::nullopt;
}

} // namespace kb::scene
