#pragma once

#include "kb/render/resources/RenderHandles.hpp"
#include "kb/render/resources/RenderResources.hpp"
#include "kb/render/runtime/RuntimeMaterialResolver.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace kb::render {

struct RuntimeAssetKey {
    std::uint64_t sceneId = 0;
    std::uint64_t assetId = 0;

    [[nodiscard]] friend constexpr bool operator==(RuntimeAssetKey lhs, RuntimeAssetKey rhs) noexcept = default;
};

struct RuntimeAssetKeyHash {
    [[nodiscard]] std::size_t operator()(RuntimeAssetKey key) const noexcept {
        const std::uint64_t mixed = key.sceneId ^ (key.assetId + 0x9e3779b97f4a7c15ULL + (key.sceneId << 6U) + (key.sceneId >> 2U));
        return static_cast<std::size_t>(mixed);
    }
};

struct RuntimeTextureAssetKey {
    std::uint64_t sceneId = 0;
    std::uint64_t assetId = 0;
    RenderTextureColorSpace colorSpace = RenderTextureColorSpace::Linear;

    [[nodiscard]] friend constexpr bool operator==(RuntimeTextureAssetKey lhs, RuntimeTextureAssetKey rhs) noexcept = default;
};

struct RuntimeTextureAssetKeyHash {
    [[nodiscard]] std::size_t operator()(RuntimeTextureAssetKey key) const noexcept {
        const std::uint64_t mixedSceneAsset = key.sceneId ^ (key.assetId + 0x9e3779b97f4a7c15ULL + (key.sceneId << 6U) + (key.sceneId >> 2U));
        const std::uint64_t color = static_cast<std::uint64_t>(key.colorSpace);
        return static_cast<std::size_t>(mixedSceneAsset ^ (color + 0x9e3779b97f4a7c15ULL + (mixedSceneAsset << 6U) + (mixedSceneAsset >> 2U)));
    }
};

struct RuntimeMeshResource {
    RenderMeshHandle handle{};
    // LIB-146: the payload this GPU mesh was built from, observed WEAKLY and never by raw
    // address. RuntimeMeshResourceEnsurer's in-place-update fast paths use "is this the same
    // live asset object?" to decide that the cached GPU buffers may be kept and only
    // sub-resources re-uploaded (terrain painting mutates the editor's published working
    // asset in place, so its contentHash changes while the object does not). A raw pointer
    // cannot express that question: when a mesh file changes on disk,
    // kb::assets::AssetDiscoveryService drops the AssetManager cache entry, the old payload
    // is freed, and the reload's std::make_shared routinely hands back the very same
    // address - an ABA that made a genuinely replaced mesh look like an in-place edit and
    // left the stale GPU buffers bound (a model edited on disk kept rendering its old
    // geometry). A weak_ptr holds the control block, so the storage it describes cannot be
    // recycled behind our back: an expired observation is honest, and a matching address
    // always means the same object.
    std::weak_ptr<const void> sourceAsset{};
    std::uint64_t sourceAssetId = 0U;
    std::uint64_t contentHash = 0;
    std::uint64_t dynamicTopologyKey = 0;
    std::size_t dynamicVertexUpdateCount = 0U;
    std::size_t dynamicSectionUpdateCount = 0U;
    std::size_t dynamicTerrainLayerWeightUpdateCount = 0U;
    std::uint64_t lastReferencedFrame = 0;
    bool dynamicVertexUpdates = false;
    bool dynamicTerrainLayerUpdates = false;

    // True only when `candidate` is the very same, still-live payload this resource was
    // built from - never merely an object that reuses its address.
    [[nodiscard]] bool HasSourceAsset(const void* candidate) const noexcept {
        const std::shared_ptr<const void> alive = sourceAsset.lock();
        return alive != nullptr && candidate != nullptr && alive.get() == candidate;
    }
};

struct RuntimeMaterialResource {
    RenderMaterialHandle handle{};
    std::uint64_t contentHash = 0;
    std::uint64_t lastReferencedFrame = 0;
    RuntimeMaterialResolveStatus status = RuntimeMaterialResolveStatus::Resolved;
    RuntimeMaterialRenderMode renderMode = RuntimeMaterialRenderMode::BuiltinPbr;
    std::vector<RuntimeMaterialResolveDiagnostic> diagnostics{};
};

struct RuntimeTextureResource {
    RenderTextureHandle handle{};
    std::uint64_t contentHash = 0;
    std::uint64_t lastReferencedFrame = 0;
    RenderTextureDimension dimension = RenderTextureDimension::Texture2D;
};

} // namespace kb::render
