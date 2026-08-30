#include "scene/assets/ScenePrefabGuidAssetIndex.hpp"

#include "engine/assets/AssetRegistry.hpp"
#include "scene/prefab/io/ScenePrefabAssetReader.hpp"

#include <string_view>
#include <utility>

namespace kb::scene {
namespace {

constexpr std::string_view kScenePrefabAssetType = "ScenePrefab";

} // namespace

const ScenePrefabGuidClaim* ScenePrefabGuidAssetIndex::Find(
    const kb::assets::AssetRegistry& registry,
    kb::assets::AssetId guidReference) {
    if (builtGeneration_ != registry.Generation()) {
        Build(registry);
    }
    const auto claim = claims_.find(guidReference.value);
    return claim == claims_.end() ? nullptr : &claim->second;
}

void ScenePrefabGuidAssetIndex::Build(const kb::assets::AssetRegistry& registry) {
    claims_.clear();
    std::string guid;
    for (const kb::assets::AssetMetadata& candidate : registry.All()) {
        if (candidate.type != kScenePrefabAssetType ||
            candidate.physicalPath.empty() ||
            !candidate.id.IsValid() ||
            !ScenePrefabAssetReader::ReadGuid(candidate.physicalPath, guid)) {
            continue;
        }

        const std::uint64_t reference = kb::assets::MakeAssetId(guid).value;
        std::string virtualPath = kb::assets::NormalizeAssetPath(candidate.virtualPath);
        const auto existing = claims_.find(reference);
        if (existing == claims_.end()) {
            claims_.emplace(reference, ScenePrefabGuidClaim{
                .assetId = candidate.id,
                .claimCount = 1U,
                .firstVirtualPath = std::move(virtualPath),
                .secondVirtualPath = {},
            });
            continue;
        }

        // A second claimant retires the guid: it can no longer name a file, and
        // it must not start naming one again just because the registry happened
        // to be walked in a different order. The two paths reported are the two
        // lexicographically smallest claimants, so the diagnostic is identical
        // on every machine.
        ScenePrefabGuidClaim& claim = existing->second;
        claim.assetId = kb::assets::AssetId{};
        ++claim.claimCount;
        if (virtualPath < claim.firstVirtualPath) {
            claim.secondVirtualPath = std::move(claim.firstVirtualPath);
            claim.firstVirtualPath = std::move(virtualPath);
        } else if (claim.secondVirtualPath.empty() || virtualPath < claim.secondVirtualPath) {
            claim.secondVirtualPath = std::move(virtualPath);
        }
    }
    builtGeneration_ = registry.Generation();
}

} // namespace kb::scene
