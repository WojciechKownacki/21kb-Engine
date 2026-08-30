#pragma once

#include "engine/assets/AssetId.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace kb::assets {
class AssetRegistry;
}

namespace kb::scene {

// What the registered prefab assets collectively say about one prefab guid.
// A guid is an identity, so exactly one file may declare it; copying a
// ".kbprefab" copies its guid, which is how two files end up claiming one
// identity. Such a guid names nothing: `assetId` stays invalid and the two
// claimant paths are kept for the diagnostic.
struct ScenePrefabGuidClaim {
    kb::assets::AssetId assetId{};
    std::uint32_t claimCount = 0U;
    std::string firstVirtualPath;
    std::string secondVirtualPath;

    [[nodiscard]] bool IsContested() const noexcept {
        return claimCount > 1U;
    }
};

// Maps MakeAssetId(prefabGuid) - the form a scene node's nested-prefab reference
// is recorded in, because a node knows its nested prefab only by guid - onto the
// prefab asset declaring that guid.
//
// Building it opens one file per registered prefab asset (guid header only), so
// it is cached against AssetRegistry::Generation(): a whole dependency-discovery
// pass upserts nothing while it walks the registry, so the index is built once
// per pass instead of once per scene, and any later registry mutation drops it.
// Instances are not thread-safe; discovery stops the async loader worker for
// exactly that reason, and code that runs off the discovery thread must use its
// own instance.
class ScenePrefabGuidAssetIndex {
public:
    // Returns nullptr when no registered prefab asset declares the guid behind
    // `guidReference`. A contested guid is reported (IsContested) rather than
    // resolved, so nothing downstream can pick a file by coin flip.
    [[nodiscard]] const ScenePrefabGuidClaim* Find(
        const kb::assets::AssetRegistry& registry,
        kb::assets::AssetId guidReference);

private:
    void Build(const kb::assets::AssetRegistry& registry);

    std::unordered_map<std::uint64_t, ScenePrefabGuidClaim> claims_;
    std::uint64_t builtGeneration_ = 0U;
};

} // namespace kb::scene
