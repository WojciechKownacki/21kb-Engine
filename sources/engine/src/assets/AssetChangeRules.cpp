#include "engine/assets/AssetChangeRules.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"

#include <string>

namespace kb::assets {
namespace {

[[nodiscard]] const AssetMetadata* FirstExternalDependent(const AssetManager& manager, AssetId target,
    const std::unordered_set<std::uint64_t>& sameOperationAssets, bool relocation) {
    const AssetMetadata* targetMetadata = manager.Registry().Find(target);
    if (!target.IsValid()) {
        return nullptr;
    }
    for (const AssetMetadata& metadata : manager.Registry().All()) {
        if (sameOperationAssets.contains(metadata.id.value)) {
            continue;
        }
        for (const AssetId dependency : metadata.dependencies) {
            if (dependency == target) {
                if (relocation && targetMetadata != nullptr && targetMetadata->type == "ScenePrefab" &&
                    (metadata.type == "Scene" || metadata.type == "ScenePrefab")) continue;
                return &metadata;
            }
        }
    }
    return nullptr;
}

} // namespace

bool AssetChangeRules::CanChange(AssetManager& manager, AssetId target, std::string_view operation,
    const std::unordered_set<std::uint64_t>& sameOperationAssets) {
    if (const AssetMetadata* dependent = FirstExternalDependent(manager, target, sameOperationAssets, operation == "moved" || operation == "renamed");
        dependent != nullptr) {
        const AssetMetadata* targetMetadata = manager.Registry().Find(target);
        const std::string name = targetMetadata != nullptr ? targetMetadata->virtualPath.generic_string() : ToString(target);
        manager.SetError(name + " can't be " + std::string{ operation } + ": " + dependent->virtualPath.generic_string() +
            " uses it. Remove that reference first.");
        return false;
    }
    return true;
}

std::vector<AssetId> AssetChangeRules::AssetsUnder(const AssetManager& manager, const std::filesystem::path& virtualFolder) {
    std::vector<AssetId> assets;
    const std::string folder = NormalizeAssetPath(virtualFolder) + "/";
    for (const AssetMetadata& metadata : manager.Registry().All()) {
        if (NormalizeAssetPath(metadata.virtualPath).starts_with(folder)) {
            assets.push_back(metadata.id);
        }
    }
    return assets;
}

bool AssetChangeRules::CanChangeFolder(AssetManager& manager, const std::filesystem::path& virtualFolder, std::string_view operation) {
    // Removing both ends of a reference is safe. Relocation changes paths even when both files move
    // together, so those references must still prevent relocation.
    const std::vector<AssetId> contents = AssetsUnder(manager, virtualFolder);
    std::unordered_set<std::uint64_t> moving;
    if (operation == "deleted") {
        for (const AssetId asset : contents) {
            moving.insert(asset.value);
        }
    }
    for (const AssetId asset : contents) {
        if (!CanChange(manager, asset, operation, moving)) {
            return false;
        }
    }
    return true;
}

bool AssetChangeRules::LeavesProtectedScenes(AssetManager& manager, const AssetProtectedScenes& protectedScenes,
    const std::filesystem::path& physical, std::string_view operation) {
    if (physical.empty()) {
        return true;
    }
    const auto holds = [&physical](const std::filesystem::path& scene) {
        if (scene.empty()) {
            return false;
        }
        const std::filesystem::path relative = scene.lexically_normal().lexically_relative(physical.lexically_normal());
        return !relative.empty() && *relative.begin() != "..";
    };
    const std::u8string name = physical.filename().u8string();
    const std::string quoted = "'" + std::string{ reinterpret_cast<const char*>(name.data()), name.size() } + "'";
    if (holds(protectedScenes.openScene)) {
        manager.SetError("Can't " + std::string{ operation } + " " + quoted +
            ": it holds the scene open in the editor. Open another scene first.");
        return false;
    }
    if (holds(protectedScenes.startupScene)) {
        manager.SetError("Can't " + std::string{ operation } + " " + quoted +
            ": it holds the project's startup scene. Choose another startup scene first.");
        return false;
    }
    return true;
}

} // namespace kb::assets
