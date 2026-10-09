#pragma once

#include "kb/render/resources/RenderHandles.hpp"
#include "kb/render/resources/RenderResources.hpp"
#include "kb/render/runtime/RuntimeRenderResourceCacheTypes.hpp"

#include "engine/assets/streaming/StreamingResidency.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace kb::assets::bake {
class RuntimeAssetPack;
}

namespace kb::assets::streaming {
class AsyncFileReader;
}

namespace kb::render {

// The runtime cache's maps (RuntimeRenderResourceCache.hpp), spelled out because the cache owns
// the streamer.
using RuntimeStreamedTextureMap = std::unordered_map<RuntimeTextureAssetKey, RuntimeTextureResource, RuntimeTextureAssetKeyHash>;
using RuntimeStreamedMeshMap = std::unordered_map<RuntimeAssetKey, RuntimeMeshResource, RuntimeAssetKeyHash>;

class RenderScene;
class SceneRenderer;
struct RenderMeshAssetData;
struct RenderTextureAssetData;
struct SceneRenderCamera;

struct RuntimeContentStreamingSettings {
    // GPU memory the streamed levels of textures and meshes may occupy together, tails and
    // coarsest levels included.
    std::uint64_t budgetBytes = 512ULL * 1024ULL * 1024ULL;
    // Dedicated I/O threads, and reads each keeps in flight.
    std::uint32_t ioWorkers = 2U;
    std::uint32_t readsInFlightPerWorker = 4U;
    // Level loads in flight at once across all resources.
    std::uint32_t maxLoadsInFlight = 32U;
    // GPU resources rebuilt with new levels per frame, and the bytes they may upload per frame,
    // so a burst of arrivals is spread over frames instead of stalling one.
    std::uint32_t maxRebuildsPerFrame = 8U;
    std::uint64_t maxUploadBytesPerFrame = 64ULL * 1024ULL * 1024ULL;
    // A mesh level is detailed enough once its simplification error projects to at most this
    // many pixels.
    float meshMaxScreenErrorPixels = 1.0F;
    // Added to every wanted texture mip; positive keeps textures coarser.
    std::int32_t textureMipBias = 0;
};

struct RuntimeContentStreamingStats {
    kb::assets::streaming::StreamingResidencyStats residency{};
    std::uint32_t streamedTextures = 0U;
    std::uint32_t streamedMeshes = 0U;
    std::uint32_t loadsInFlight = 0U;
    std::uint64_t rebuilds = 0U;
    std::uint64_t uploadedBytes = 0U;
    std::uint64_t failedLoads = 0U;
    // Time from issuing a level load to having its decoded bytes on the render thread.
    double lastLoadMilliseconds = 0.0;
    double maxLoadMilliseconds = 0.0;
};

// One frame of what the renderer sees, for streaming decisions.
struct RuntimeContentStreamingFrame {
    std::uint64_t sceneId = 0U;
    const RenderScene* renderScene = nullptr;
    const SceneRenderCamera* camera = nullptr;
    std::uint32_t viewportHeight = 0U;
    std::uint64_t frame = 0U;
};

// Streams the finer mip levels of packaged textures and the finer levels of detail of packaged
// meshes in and out of GPU memory under one budget.
//
// A streamed texture is created from its mip tail and a streamed mesh from its coarsest level
// (the loaders keep those resident); the runtime ensurers hand the new GPU resource to Track*.
// Every frame Update:
//   1. measures, from the scene's mesh proxies and the camera, how large each streamed mesh and
//      each material's textures are on screen, and asks the residency manager for the levels
//      that size needs (a texture mip per halving of its on-screen size, a mesh level whose
//      error projects under meshMaxScreenErrorPixels), most important first;
//   2. starts the level loads the manager plans on the I/O pool -- the pool reads, verifies,
//      decrypts and decompresses each pack block -- and applies its evictions;
//   3. collects finished loads without waiting for any, and rebuilds at most
//      maxRebuildsPerFrame GPU resources with the levels now resident, swapping the new handle
//      into the runtime cache and the scene's resource map.
// The render thread never waits for a disk or a decoder.
class RuntimeContentStreamer final {
public:
    RuntimeContentStreamer();
    RuntimeContentStreamer(const RuntimeContentStreamer&) = delete;
    RuntimeContentStreamer& operator=(const RuntimeContentStreamer&) = delete;
    ~RuntimeContentStreamer();

    void Configure(const RuntimeContentStreamingSettings& settings);
    [[nodiscard]] const RuntimeContentStreamingSettings& Settings() const noexcept {
        return settings_;
    }

    // A texture created from the tail of `asset`, whose layout says which levels stream. Does
    // nothing for an asset without streamed levels or without a pack.
    void TrackTexture(
        RuntimeTextureAssetKey key,
        RenderTextureHandle handle,
        std::shared_ptr<const RenderTextureAssetData> asset,
        std::shared_ptr<kb::assets::bake::RuntimeAssetPack> pack);
    // A mesh created from the coarsest level of `asset` with these effective material slots
    // (the asset's own, with embedded materials filled in), which every rebuild keeps.
    void TrackMesh(
        RuntimeAssetKey key,
        RenderMeshHandle handle,
        std::shared_ptr<const RenderMeshAssetData> asset,
        std::shared_ptr<kb::assets::bake::RuntimeAssetPack> pack,
        std::vector<RenderMaterialSlotDesc> materialSlots);
    [[nodiscard]] bool IsTracking(RuntimeTextureAssetKey key) const noexcept;
    [[nodiscard]] bool IsTracking(RuntimeAssetKey key) const noexcept;

    // The texture ensurer reports which textures each material it ensured this frame samples;
    // `materialAssetId` 0 is a texture used without a material (screen UI, the sky), which
    // wants its full detail.
    void NoteTextureUse(RuntimeTextureAssetKey key, std::uint64_t materialAssetId);

    // Wants of a texture or mesh set directly, merged with the frame's feedback.
    void RequestTexture(RuntimeTextureAssetKey key, std::uint32_t level, float priority, std::uint64_t frame);
    void RequestMesh(RuntimeAssetKey key, std::uint32_t level, float priority, std::uint64_t frame);

    void Update(
        const RuntimeContentStreamingFrame& frame,
        SceneRenderer& sceneRenderer,
        RuntimeStreamedTextureMap& textures,
        RuntimeStreamedMeshMap& meshes);

    // Forgets every resource of the scene / everything (the cache released them).
    void ReleaseScene(std::uint64_t sceneId) noexcept;
    void ReleaseAll() noexcept;

    // The full-chain level the texture's GPU resource currently starts at, or the baked level
    // the mesh's GPU resource currently starts at; -1 when not tracked.
    [[nodiscard]] std::int32_t UploadedLevel(RuntimeTextureAssetKey key) const noexcept;
    [[nodiscard]] std::int32_t UploadedLevel(RuntimeAssetKey key) const noexcept;

    [[nodiscard]] RuntimeContentStreamingStats Stats() const;

private:
    struct Record;
    struct PendingLoad;

    [[nodiscard]] Record* FindTexture(RuntimeTextureAssetKey key) noexcept;
    [[nodiscard]] Record* FindMesh(RuntimeAssetKey key) noexcept;
    void Forget(std::uint64_t id) noexcept;
    void Request(Record& record, std::uint32_t level, float priority, std::uint64_t frame);
    void GatherFeedback(const RuntimeContentStreamingFrame& frame, const SceneRenderer& sceneRenderer);
    void CollectLoads(std::uint64_t frame);
    void StartLoads(const kb::assets::streaming::StreamingPlan& plan);
    void ApplyEvictions(const kb::assets::streaming::StreamingPlan& plan);
    void Rebuild(SceneRenderer& sceneRenderer, RuntimeStreamedTextureMap& textures, RuntimeStreamedMeshMap& meshes);
    [[nodiscard]] bool RebuildTexture(Record& record, SceneRenderer& sceneRenderer, RuntimeStreamedTextureMap& textures,
        std::uint32_t level, std::uint64_t& uploaded);
    [[nodiscard]] bool RebuildMesh(Record& record, SceneRenderer& sceneRenderer, RuntimeStreamedMeshMap& meshes,
        std::uint32_t level, std::uint64_t& uploaded);
    void EnsureReader();

    RuntimeContentStreamingSettings settings_{};
    kb::assets::streaming::StreamingResidencyManager residency_;
    std::unique_ptr<kb::assets::streaming::AsyncFileReader> reader_;
    std::unordered_map<std::uint64_t, std::unique_ptr<Record>> records_;
    std::unordered_map<RuntimeTextureAssetKey, std::uint64_t, RuntimeTextureAssetKeyHash> textureIds_;
    std::unordered_map<RuntimeAssetKey, std::uint64_t, RuntimeAssetKeyHash> meshIds_;
    // This frame's material -> streamed textures, from NoteTextureUse.
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> materialTextures_;
    std::vector<std::uint64_t> materialLessTextures_;
    std::vector<std::unique_ptr<PendingLoad>> pending_;
    std::uint64_t nextId_ = 1U;
    std::uint64_t lastPlannedFrame_ = ~0ULL;
    RuntimeContentStreamingStats counters_{};
};

} // namespace kb::render
