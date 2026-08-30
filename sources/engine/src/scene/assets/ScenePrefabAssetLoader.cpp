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
void AppendNestedPrefab(
    std::vector<kb::assets::AssetId>& dependencies,
    ScenePrefabGuidAssetIndex& guidIndex,
    const kb::assets::AssetRegistry& registry,
    const std::string& guid) {
    if (guid.empty()) {
        return;
    }
    const ScenePrefabGuidClaim* claim = guidIndex.Find(registry, kb::assets::MakeAssetId(guid));
    if (claim != nullptr) {
        AppendUnique(dependencies, claim->assetId);
    }
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
    std::vector<kb::assets::AssetId>& dependencies,
    ScenePrefabGuidAssetIndex& guidIndex,
    const kb::assets::AssetRegistry& registry,
    const ScenePrefab& prefab) {
    for (const ScenePrefabNodeDesc& node : prefab.Nodes()) {
        SceneComponentAssetReferences::ForEachReference(
            node.components,
            [&dependencies](std::uint64_t rawId, std::string_view role) {
                static_cast<void>(role);
                AppendUnique(dependencies, kb::assets::AssetId{ rawId });
            });
        AppendNestedPrefab(dependencies, guidIndex, registry, node.nestedPrefabGuid);
        AppendOverrideReferences(dependencies, registry, node.nestedPrefabOverrides);
    }
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

    std::vector<kb::assets::AssetId> dependencies;
    AppendNodeReferences(dependencies, nestedPrefabGuidIndex_, registry, asset.prefab);
    if (asset.kind == ScenePrefabAssetKind::Variant) {
        // A variant carries no nodes of its own: what it needs is its base prefab,
        // the subtrees it adds on top of it, and any asset its overrides swap in.
        AppendNestedPrefab(dependencies, nestedPrefabGuidIndex_, registry, asset.baseGuid);
        AppendOverrideReferences(dependencies, registry, asset.overrides);
        for (const ScenePrefabVariantAddedSubtree& added : asset.addedChildren) {
            AppendNodeReferences(dependencies, nestedPrefabGuidIndex_, registry, added.subtree);
        }
    }
    return dependencies;
}

} // namespace kb::scene
