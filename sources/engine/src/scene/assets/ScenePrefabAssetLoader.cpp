#include "scene/assets/ScenePrefabAssetLoader.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMemoryInputStream.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "scene/asset/SceneComponentAssetReferences.hpp"
#include "scene/prefab/io/ScenePrefabAssetReader.hpp"
#include "scene/prefab/io/ScenePrefabAssetService.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace kb::scene {
namespace {

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

// A prefab node's nested-prefab reference is a guid, exactly like a scene node's,
// and resolves through the same index - including its refusal to name a file for
// a guid two prefab files both declare.
struct ScenePrefabDependencyResolution {
    std::vector<kb::assets::AssetId> dependencies;
    std::optional<std::string> diagnostic;
};

void AppendNestedPrefab(
    ScenePrefabDependencyResolution& result,
    ScenePrefabGuidAssetIndex& guidIndex,
    const kb::assets::AssetRegistry& registry,
    const std::string& guid) {
    if (guid.empty()) {
        return;
    }
    const ScenePrefabGuidClaim* claim = guidIndex.Find(registry, kb::assets::MakeAssetId(guid));
    if (claim == nullptr) {
        if (!result.diagnostic.has_value()) {
            result.diagnostic = "references nested prefab guid \"" + guid +
                "\" which no registered ScenePrefab asset declares";
        }
        return;
    }
    if (claim->IsContested()) {
        if (!result.diagnostic.has_value()) {
            result.diagnostic = "references nested prefab guid \"" + guid +
                "\" which is declared by " + std::to_string(claim->claimCount) +
                " prefab assets (" + claim->firstVirtualPath + ", " +
                claim->secondVirtualPath + ") and cannot be resolved deterministically";
        }
        return;
    }
    AppendUnique(result.dependencies, claim->assetId);
}

// Every property path that names an asset in this engine ends in "assetId",
// optionally followed by a slot index ("meshRenderer.materialSlotAssetId.3") -
// the convention ScenePrefabAppliedPropertyBuilder and the override reporters
// write and read. A variant's override list stores values as untyped text, so
// that suffix is the only thing that marks one as an asset reference.
[[nodiscard]] bool IsAssetIdPropertyPath(std::string_view path) noexcept {
    const std::size_t lastDot = path.rfind('.');
    if (lastDot != std::string_view::npos && lastDot + 1U < path.size() &&
        std::all_of(path.begin() + static_cast<std::ptrdiff_t>(lastDot) + 1, path.end(), [](char character) noexcept {
            return character >= '0' && character <= '9';
        })) {
        path = path.substr(0U, lastDot);
    }

    constexpr std::string_view kSuffix = "assetid";
    if (path.size() <= kSuffix.size()) {
        return false;
    }
    const std::string_view tail = path.substr(path.size() - kSuffix.size());
    return std::equal(tail.begin(), tail.end(), kSuffix.begin(), [](char left, char right) noexcept {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(left))) == right;
    });
}

// An override value is author-supplied text, so it becomes an edge only when it
// parses as an id the registry actually holds. Emitting an unparsed or unknown
// value would put an id that resolves to nothing into the graph and turn every
// such override into a phantom missing dependency.
void AppendOverrideReferences(
    std::vector<kb::assets::AssetId>& dependencies,
    const kb::assets::AssetRegistry& registry,
    const std::vector<ScenePrefabPropertyOverride>& overrides) {
    for (const ScenePrefabPropertyOverride& property : overrides) {
        if (!IsAssetIdPropertyPath(property.propertyPath)) {
            continue;
        }
        std::uint64_t rawId = 0U;
        const char* const begin = property.value.data();
        const char* const end = begin + property.value.size();
        const std::from_chars_result parsed = std::from_chars(begin, end, rawId);
        if (parsed.ec != std::errc{} || parsed.ptr != end || rawId == 0U) {
            continue;
        }
        const kb::assets::AssetId id{ rawId };
        if (registry.Find(id) != nullptr) {
            AppendUnique(dependencies, id);
        }
    }
}

void AppendNodeReferences(
    ScenePrefabDependencyResolution& result,
    ScenePrefabGuidAssetIndex& guidIndex,
    const kb::assets::AssetRegistry& registry,
    const ScenePrefab& prefab) {
    for (const ScenePrefabNodeDesc& node : prefab.Nodes()) {
        SceneComponentAssetReferences::ForEachReference(
            node.components,
            [&result](std::uint64_t rawId, std::string_view role) {
                static_cast<void>(role);
                AppendUnique(result.dependencies, kb::assets::AssetId{ rawId });
            });
        AppendNestedPrefab(result, guidIndex, registry, node.nestedPrefabGuid);
        AppendOverrideReferences(result.dependencies, registry, node.nestedPrefabOverrides);
    }
}

[[nodiscard]] ScenePrefabDependencyResolution ResolveScenePrefabDependencies(
    const ScenePrefabAssetReadResult& asset,
    const kb::assets::AssetRegistry& registry,
    ScenePrefabGuidAssetIndex& guidIndex) {
    ScenePrefabDependencyResolution result;
    AppendNodeReferences(result, guidIndex, registry, asset.prefab);
    if (asset.kind == ScenePrefabAssetKind::Variant) {
        AppendNestedPrefab(result, guidIndex, registry, asset.baseGuid);
        AppendOverrideReferences(result.dependencies, registry, asset.overrides);
        for (const ScenePrefabVariantAddedSubtree& added : asset.addedChildren) {
            AppendNodeReferences(result, guidIndex, registry, added.subtree);
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

ScenePrefabAssetLoader::ScenePrefabAssetLoader(Scene& scene) noexcept
    : scene_(scene) {}

std::string_view ScenePrefabAssetLoader::Type() const noexcept {
    return "ScenePrefab";
}

std::type_index ScenePrefabAssetLoader::PayloadType() const noexcept {
    return typeid(ScenePrefab);
}

std::vector<std::string> ScenePrefabAssetLoader::Extensions() const {
    return { ".kbprefab" };
}

kb::assets::AssetLoadResult ScenePrefabAssetLoader::Load(const kb::assets::AssetLoadRequest& request) {
    ScenePrefabHandle handle;
    if (request.IsPackaged()) {
        std::vector<std::uint8_t> sourceBytes;
        std::string error;
        if (!request.ReadSourceBytes(sourceBytes, error)) {
            return kb::assets::AssetLoadResult{ .asset = {}, .error = std::move(error) };
        }
        kb::assets::AssetMemoryInputStream input{ sourceBytes };
        ScenePrefabAssetReadResult asset;
        if (!ScenePrefabAssetReader::Read(input, asset)) {
            return kb::assets::AssetLoadResult{ .asset = {}, .error = "Scene prefab asset parse failed" };
        }
        handle = ScenePrefabAssetService::LoadReadOnly(
            scene_, std::move(asset), request.metadata.virtualPath.generic_string());
    } else {
        handle = ScenePrefabAssetService::Load(scene_, request.resolvedPath);
    }
    if (!handle.IsValid()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = "Scene prefab asset load failed" };
    }

    ScenePrefab prefab = scene_.Prefabs().Get(handle);
    if (prefab.Empty()) {
        return kb::assets::AssetLoadResult{ .asset = {}, .error = "Scene prefab asset was empty after load" };
    }

    return kb::assets::AssetLoadResult{ .asset = std::make_shared<ScenePrefab>(std::move(prefab)), .error = {} };
}

std::vector<kb::assets::AssetId> ScenePrefabAssetLoader::DiscoverDependencies(
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetRegistry& registry) const {
    // Without this the dependency graph stops at the prefab: a scene names the
    // prefab it nests, but the mesh, material, skeleton or nested prefab that
    // prefab itself renders would be one hop past the end of the graph and the
    // cooker would package the prefab with none of its art. The prefab file is the
    // only source for those references - a prefab has no ".meta" sidecar - so it
    // is parsed here, once per discovery pass and only for prefab assets.
    ScenePrefabAssetReadResult asset;
    if (metadata.physicalPath.empty() || !ScenePrefabAssetReader::Read(metadata.physicalPath, asset)) {
        // An unreadable or malformed prefab file declares no dependencies; Load()
        // still rejects it on its own terms when the prefab is actually opened.
        return {};
    }

    return ResolveScenePrefabDependencies(asset, registry, nestedPrefabGuidIndex_).dependencies;
}

std::optional<std::string> ScenePrefabAssetLoader::ValidateDependencies(
    const kb::assets::AssetMetadata& metadata,
    const kb::assets::AssetRegistry& registry) const {
    ScenePrefabAssetReadResult asset;
    if (metadata.physicalPath.empty() || !ScenePrefabAssetReader::Read(metadata.physicalPath, asset)) {
        return "has no readable scene prefab source \"" + metadata.physicalPath.generic_string() + "\"";
    }

    ScenePrefabGuidAssetIndex guidIndex;
    ScenePrefabDependencyResolution current = ResolveScenePrefabDependencies(asset, registry, guidIndex);
    if (current.diagnostic.has_value()) {
        return current.diagnostic;
    }
    if (SortedDependencyIds(current.dependencies) != SortedDependencyIds(metadata.dependencies)) {
        return "dependency metadata changed after asset discovery; retry before cooking";
    }
    return std::nullopt;
}

std::optional<std::string> ScenePrefabAssetLoader::ValidateRuntimeDependencies(
    const kb::assets::AssetLoadRequest& request,
    const kb::assets::AssetRegistry& registry) const {
    // Runtime packages carry the dependency closure in their authenticated manifest,
    // while prefab guid discovery currently reads loose prefab headers. The cooker
    // validates every prefab before creating that manifest.
    if (request.IsPackaged()) {
        return std::nullopt;
    }
    return ValidateDependencies(request.metadata, registry);
}

} // namespace kb::scene
