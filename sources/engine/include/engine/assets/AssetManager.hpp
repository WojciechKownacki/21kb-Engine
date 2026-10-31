#pragma once

#include "engine/assets/AssetCompatibility.hpp"
#include "engine/assets/AssetHandle.hpp"
#include "engine/assets/AssetManifest.hpp"
#include "engine/assets/AssetMountTable.hpp"
#include "engine/assets/IAssetLoader.hpp"

#include <atomic>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace kb::assets {

class AssetDiscoveryService;
class AssetRuntimeLoadService;
class AssetRegistrySnapshotCache;
namespace bake {
class RuntimeAssetPack;
}
namespace streaming {
class BackgroundLane;
}

// LIB-158: how the runtime cache retains a loaded asset's payload.
//   Retain (default): the cache holds its own strong reference — the payload
//     stays resident until an explicit Unload(id), exactly the pre-LIB-158
//     behaviour. Load handles are additional strong holders.
//   ReleaseWhenUnreferenced: the cache holds only a weak reference — the
//     payload lives exactly as long as at least one AssetHandle<T> (strong
//     holder) is alive, and is freed the moment the last one drops. A later
//     Load reloads it from disk. IsLoaded/ReferenceCount report the live
//     state; PruneUnreferenced sweeps the now-empty map entries.
enum class AssetUnloadPolicy : std::uint8_t {
    Retain,
    ReleaseWhenUnreferenced,
};

enum class AsyncAssetLoadStatus : std::uint8_t {
    NotRequested,
    Pending,
    Completed,
    Failed,
};

class AssetOpaqueHandle {
public:
    AssetOpaqueHandle() = default;
    AssetOpaqueHandle(AssetId id, std::shared_ptr<void> payload) noexcept
        : id_(id), payload_(std::move(payload)) {}

    [[nodiscard]] AssetId Id() const noexcept { return id_; }
    [[nodiscard]] bool IsLoaded() const noexcept { return id_.IsValid() && payload_ != nullptr; }

private:
    AssetId id_{};
    std::shared_ptr<void> payload_{};
};

[[nodiscard]] std::string_view ToString(AssetUnloadPolicy policy) noexcept;
[[nodiscard]] bool TryParseAssetUnloadPolicy(std::string_view name, AssetUnloadPolicy& out) noexcept;

struct AssetMoveResult {
    bool succeeded = false;
    std::filesystem::path virtualPath{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return succeeded;
    }
};

class AssetManager {
public:
    AssetManager();
    ~AssetManager();

    [[nodiscard]] AssetMountTable& Mounts() noexcept;
    [[nodiscard]] const AssetMountTable& Mounts() const noexcept;
    [[nodiscard]] AssetRegistry& Registry() noexcept;
    [[nodiscard]] const AssetRegistry& Registry() const noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept;

    [[nodiscard]] bool RegisterLoader(std::unique_ptr<IAssetLoader> loader);
    // Called by stable host code; dynamic modules use EngineModuleContext::RegisterAssetLoader
    // so the wrapper and shared ownership control blocks also belong to the host.
    [[nodiscard]] bool RegisterLoader(std::unique_ptr<IAssetLoader> loader, std::shared_ptr<void> libraryLifetime);
    [[nodiscard]] bool UnregisterLoader(std::string_view type);
    [[nodiscard]] bool RegisterAsset(AssetMetadata metadata);
    // File-less derived resources owned by a runtime system. They cannot overwrite authored
    // assets, including immutable package entries. The owner releases them when it detaches.
    [[nodiscard]] bool RegisterGeneratedAsset(AssetMetadata metadata);
    [[nodiscard]] bool ReleaseGeneratedAsset(AssetId id);
    // Enters immutable packaged-runtime mode and transactionally rebuilds the registry from
    // the validated manifest. Packaged mode never resolves a physical path or falls back to
    // discovery; loader requests retain shared ownership of this exact pack.
    [[nodiscard]] bool MountRuntimePack(std::shared_ptr<bake::RuntimeAssetPack> pack);
    [[nodiscard]] bool IsRuntimePackMounted() const noexcept;
    [[nodiscard]] std::shared_ptr<bake::RuntimeAssetPack> RuntimePack() const noexcept;
    // Refreshes one already-registered asset after an atomic authoring write.
    // Unlike DiscoverMountedAssets(), this hashes and re-analyses only the
    // requested file while preserving registry and cache coherency.
    [[nodiscard]] bool RefreshAsset(AssetId id);
    [[nodiscard]] std::size_t DiscoverMountedAssets();
    [[nodiscard]] std::vector<std::filesystem::path> VirtualFolders() const;
    [[nodiscard]] bool CreateFolder(const std::filesystem::path& virtualFolder);
    [[nodiscard]] std::optional<std::filesystem::path> CreateUniqueFolder(const std::filesystem::path& parentVirtualFolder, std::string baseName);
    [[nodiscard]] bool RenameFolder(const std::filesystem::path& virtualFolder, std::string newName);
    [[nodiscard]] bool DeleteFolder(const std::filesystem::path& virtualFolder);
    [[nodiscard]] bool RenameAsset(AssetId id, std::string newName);
    [[nodiscard]] AssetMoveResult MoveAssetIntoFolder(AssetId id, const std::filesystem::path& destinationVirtualFolder);
    [[nodiscard]] bool MoveAsset(AssetId id, const std::filesystem::path& destinationVirtualFolder);
    [[nodiscard]] AssetMoveResult MoveFolderIntoFolder(const std::filesystem::path& sourceVirtualFolder, const std::filesystem::path& destinationVirtualFolder);
    [[nodiscard]] bool MoveFolder(const std::filesystem::path& sourceVirtualFolder, const std::filesystem::path& destinationVirtualFolder);
    [[nodiscard]] bool DeleteAsset(AssetId id);

    template <typename T>
    [[nodiscard]] AssetHandle<T> Load(AssetId id) {
        std::shared_ptr<const T> payload = LoadTyped<T>(id);
        return AssetHandle<T>{ id, std::move(payload) };
    }

    template <typename T>
    [[nodiscard]] AssetHandle<T> Load(const std::filesystem::path& virtualPath) {
        const AssetMetadata* metadata = registry_.FindByPath(virtualPath);
        return metadata == nullptr ? AssetHandle<T>{} : Load<T>(metadata->id);
    }

    // Publishes an already-decoded payload for a registered asset on the owner
    // thread. Editor working copies use this to preview an asset without
    // serializing it to disk on every pointer sample. The payload type must
    // match the registered loader, so consumers still observe the same runtime
    // contract as a canonical Load<T>(). Metadata/content-hash ownership stays
    // with the caller and must be updated before publishing when render
    // consumers need to rebuild derived resources.
    template <typename T>
    [[nodiscard]] bool PublishRuntimeAsset(
        AssetId id,
        std::shared_ptr<T> payload) {
        lastError_.clear();
        const AssetMetadata* metadata = registry_.Find(id);
        IAssetLoader* loader = metadata == nullptr ? nullptr : LoaderForType(metadata->type);
        if (!id.IsValid() || payload == nullptr || metadata == nullptr ||
            loader == nullptr || loader->PayloadType() != typeid(T)) {
            lastError_ = !id.IsValid()
                ? "Invalid asset id"
                : payload == nullptr
                    ? "Published asset payload is null"
                    : metadata == nullptr
                        ? "Asset is not registered"
                        : loader == nullptr
                            ? "No loader registered for asset type: " + metadata->type
                            : "Published asset payload type does not match its loader";
            return false;
        }

        ++asyncLoadGenerations_[id.value];
        asyncLoads_.erase(id.value);
        asyncLoadErrors_.erase(id.value);
        std::shared_ptr<void> erased = loader->RetainPayload(std::move(payload));
        TrackAssetMemory(erased, std::nullopt);
        cache_[id.value] = CachedAsset{
            .retained = erased,
            .weak = erased,
            .typeName = typeid(T).name(),
            .policy = AssetUnloadPolicy::Retain,
            .runtimePublished = true,
        };
        return true;
    }

    // Type-erased force-load: caches the payload through whatever loader is
    // registered for the asset's own metadata.type, without the caller
    // knowing (or being able to name) the C++ payload type at compile time.
    // This is the ONLY way kb_engine code can force-load an asset whose
    // payload type lives in another module (e.g. kb_render's RenderMesh/
    // Material/Texture types) — the generic script-facing Assets.Load
    // surface (ScriptAssetsApi) is the reason this exists. Trusts the
    // registered loader's own IAssetLoader::PayloadType() as the expected
    // type, so it never mismatches by construction. Returns false (with
    // LastError() set) for an invalid id, unregistered asset, or an asset
    // type with no registered loader; matches Load<T>'s existing error
    // reporting via the same LoadUntyped path.
    [[nodiscard]] bool LoadOpaque(AssetId id);
    [[nodiscard]] AssetOpaqueHandle LoadOpaqueHandle(AssetId id);
    [[nodiscard]] AssetOpaqueHandle AcquireOpaqueHandle(AssetId id) const;

    template <typename T>
    [[nodiscard]] bool LoadAsync(AssetId id) {
        const AssetMetadata* metadata = registry_.Find(id);
        IAssetLoader* loader = metadata == nullptr ? nullptr : LoaderForType(metadata->type);
        if (loader == nullptr || loader->PayloadType() != typeid(T)) {
            lastError_ = metadata == nullptr ? "Asset is not registered" : "Asset type does not match async load request";
            return false;
        }
        return RequestLoadAsync(id);
    }

    template <typename T>
    [[nodiscard]] bool LoadAsync(const std::filesystem::path& virtualPath) {
        const AssetMetadata* metadata = registry_.FindByPath(virtualPath);
        if (metadata == nullptr) {
            lastError_ = "Asset is not registered";
            return false;
        }
        return LoadAsync<T>(metadata->id);
    }

    template <typename T>
    [[nodiscard]] AssetHandle<T> AcquireLoaded(AssetId id) const {
        const auto cached = cache_.find(id.value);
        if (cached == cache_.end() || cached->second.typeName != typeid(T).name()) {
            return {};
        }
        std::shared_ptr<void> payload = cached->second.weak.lock();
        return payload == nullptr
            ? AssetHandle<T>{}
            : AssetHandle<T>{ id, std::static_pointer_cast<const T>(std::move(payload)) };
    }

    // Returns only an explicitly published payload; a cached disk or package load is not a publication.
    template <typename T>
    [[nodiscard]] AssetHandle<T> AcquirePublishedRuntimeAsset(AssetId id) const {
        const auto cached = cache_.find(id.value);
        return cached != cache_.end() && cached->second.runtimePublished
            ? AcquireLoaded<T>(id) : AssetHandle<T>{};
    }

    [[nodiscard]] bool RequestLoadAsync(AssetId id, AssetUnloadPolicy policy = AssetUnloadPolicy::Retain);
    // How many asynchronous loads of this manager may run at once (default 1). A loader that does not return true
    // from AllowsConcurrentLoad() still runs one load at a time whatever the number. Only valid before the first
    // asynchronous request; returns false afterwards or for 0.
    [[nodiscard]] bool SetAsyncLoadConcurrency(std::uint32_t concurrency) noexcept;
    // Profile of the asynchronous loads so far: how many ran under the loader mutex, how many ran without it, and the
    // most that ran at the same moment.
    struct AsyncLoadConcurrencyStats {
        std::uint64_t serializedLoads = 0U;
        std::uint64_t concurrentLoads = 0U;
        std::uint32_t peakSimultaneousLoads = 0U;
    };
    [[nodiscard]] AsyncLoadConcurrencyStats AsyncLoadConcurrency() const noexcept;
    struct AsyncLoadLifecycleStats {
        std::uint64_t cancelledBeforeLoad = 0U;
        std::uint64_t cancelledDuringLoad = 0U;
        std::uint64_t discardedResults = 0U;
        std::uint64_t publishedLoads = 0U;
    };
    [[nodiscard]] AsyncLoadLifecycleStats AsyncLoadLifecycle() const noexcept;
    void PumpAsyncLoads();
    [[nodiscard]] AsyncAssetLoadStatus AsyncLoadStatus(AssetId id) const noexcept;
    [[nodiscard]] std::string AsyncLoadError(AssetId id) const;

    [[nodiscard]] bool Unload(AssetId id) noexcept;
    // Monotonic invalidation generation for a payload. Runtime consumers that
    // retain AssetHandles can compare this value to detect an explicit unload
    // and rebuild derived bindings from the newly loaded canonical asset.
    [[nodiscard]] std::uint64_t LoadGeneration(AssetId id) const noexcept;
    [[nodiscard]] bool IsLoaded(AssetId id) const noexcept;
    [[nodiscard]] std::size_t LoadedCount() const noexcept;
    struct AssetMemoryStats {
        std::uint64_t payloadBytes = 0U;
        std::size_t measuredPayloads = 0U;
        std::size_t unmeasuredPayloads = 0U;
    };
    // Published payloads still held by the cache or external handles, including after Unload().
    // Pending decoder work is excluded. Unknown payload sizes are counted explicitly.
    [[nodiscard]] AssetMemoryStats MemoryUsage();

    // LIB-158: the number of live strong holders (AssetHandle<T>/AssetRef<T>)
    // of a cached asset's payload — 0 if the asset is not cached or its
    // payload has already been released. Does NOT count the cache's own
    // Retain reference, so under Retain this is "external holders" and under
    // ReleaseWhenUnreferenced it is the full holder count. Worker results are
    // published only by PumpAsyncLoads on the owner thread.
    [[nodiscard]] std::size_t ReferenceCount(AssetId id) const noexcept;
    // The cache retention policy for a cached asset (Retain if the asset is
    // not cached — the default a fresh Load would use).
    [[nodiscard]] AssetUnloadPolicy UnloadPolicy(AssetId id) const noexcept;
    // Changes the retention policy of an already-cached asset. Returns false
    // if the asset is not currently cached (Load it first). Switching to
    // ReleaseWhenUnreferenced drops the cache's strong reference immediately
    // — if no AssetHandle holds the payload at that moment, it is freed and
    // the entry pruned right away; otherwise it lives until the last handle
    // drops. Switching back to Retain re-pins the still-live payload.
    [[nodiscard]] bool SetUnloadPolicy(AssetId id, AssetUnloadPolicy policy);
    // Removes cache entries whose payload has already been released (their
    // last strong holder dropped under ReleaseWhenUnreferenced), keeping
    // LoadedCount and the map honest. Returns the number of entries removed.
    std::size_t PruneUnreferenced() noexcept;

    // LIB-158: a non-owning weak reference to a currently-cached asset of
    // type T — empty if the asset is not cached, its payload was released, or
    // it was cached under a different type. See kb::assets::WeakAssetHandle.
    template <typename T>
    [[nodiscard]] WeakAssetHandle<T> WeakHandle(AssetId id) const {
        const auto cached = cache_.find(id.value);
        if (cached == cache_.end() || cached->second.typeName != typeid(T).name()) {
            return {};
        }
        std::shared_ptr<void> alive = cached->second.weak.lock();
        if (alive == nullptr) {
            return {};
        }
        return WeakAssetHandle<T>{ id, std::static_pointer_cast<const T>(std::move(alive)) };
    }

    [[nodiscard]] bool HasLoaderForType(std::string_view type) const noexcept;

    // LIB-159: validate that `id` and its entire declared dependency closure
    // can be loaded in this runtime, WITHOUT loading anything. Reports a
    // MissingDependency for the asset itself or any declared dependency that
    // is not registered, and an IncompatibleType for any registered asset in
    // the closure whose type has no loader here. Registry-level only (no disk
    // I/O): it does not check that a file still exists on disk (see the asset
    // reload lifecycle work for content-hash freshness) — it answers "are all
    // the pieces present and loadable in this runtime." Cycle-safe. The
    // report is compatible exactly when it has no diagnostics. It also does
    // NOT consider AssetMetadata::runtimeLoadable (an asset legitimately
    // flagged editor-only is not an incompatibility) — only presence and a
    // usable loader. A missing dependency referenced by several distinct
    // parents is reported once per depending parent, so each diagnostic
    // names a concrete owner of the problem.
    [[nodiscard]] AssetCompatibilityReport ValidateCompatibility(AssetId id) const;
    // Convenience: ValidateCompatibility(id).compatible.
    [[nodiscard]] bool IsCompatible(AssetId id) const;

    [[nodiscard]] std::string LastError() const;
    void SetError(std::string error) const;
    void ClearRuntimeCache() noexcept;
    void Clear() noexcept;

private:
    friend class AssetDiscoveryService;
    friend class AssetRuntimeLoadService;

    void TrackAssetMemory(const std::shared_ptr<void>& payload, std::optional<std::uint64_t> bytes);
    std::map<std::weak_ptr<void>, std::optional<std::uint64_t>, std::owner_less<std::weak_ptr<void>>> memoryAllocations_;
    std::uint32_t memoryPublications_ = 0U;

    struct CachedAsset {
        // Strong reference the cache itself holds — present only under
        // AssetUnloadPolicy::Retain. Null under ReleaseWhenUnreferenced, so
        // the payload's lifetime is governed entirely by external handles.
        std::shared_ptr<void> retained;
        // Always tracks the payload without extending its lifetime — the
        // source of truth for "is this asset still alive" (IsLoaded) and the
        // holder count (ReferenceCount).
        std::weak_ptr<void> weak;
        // Own the RTTI name instead of retaining std::type_index. A type_info
        // object emitted by a hot-reloaded module stops existing when that
        // module is unloaded, while the cache intentionally survives reloads.
        std::string typeName;
        AssetUnloadPolicy policy = AssetUnloadPolicy::Retain;
        bool runtimePublished = false;
    };

    struct AsyncPreparedAsset {
        AssetLoadResult result;
        bool discarded = false;
        std::string typeName;
    };

    struct AsyncPreparedState {
        std::shared_ptr<AssetLoadCancellation> cancellation = std::make_shared<AssetLoadCancellation>();
        std::atomic_bool started{false};
        std::atomic_bool accounted{false};
        std::mutex mutex;
        std::optional<AsyncPreparedAsset> prepared;
    };

    struct AsyncLoadRecord {
        std::shared_ptr<AsyncPreparedState> state;
        AssetUnloadPolicy policy = AssetUnloadPolicy::Retain;
        std::uint64_t generation = 0U;
        std::uint64_t registryGeneration = 0U;
    };

    struct AsyncLoadJob {
        IAssetLoader* loader = nullptr;
        AssetMetadata metadata;
        std::filesystem::path resolvedPath;
        std::shared_ptr<bake::RuntimeAssetPack> runtimePack;
        std::string typeName;
        std::shared_ptr<const AssetRegistry> registry;
        std::shared_ptr<AsyncPreparedState> state;
    };

    // Asynchronous loads run as jobs of one lane of the engine's background load service, so a
    // manager's loaders run one at a time and never on the owner thread.
    void QueueAsyncLoad(AsyncLoadJob job);
    // Cancels the queued loads and waits for the one running, so the caller may change the
    // loaders or the registry.
    void StopAsyncLoads() noexcept;
    void RestartAsyncLoads();
    void PrepareAsyncLoad(AsyncLoadJob job) noexcept;
    [[nodiscard]] std::shared_ptr<void> LoadUntyped(AssetId id, std::type_index expectedType);

    template <typename T>
    [[nodiscard]] std::shared_ptr<const T> LoadTyped(AssetId id) {
        std::shared_ptr<void> payload = LoadUntyped(id, typeid(T));
        if (payload == nullptr) {
            return {};
        }
        return std::static_pointer_cast<const T>(payload);
    }

    [[nodiscard]] IAssetLoader* LoaderForType(std::string_view type) const noexcept;
    [[nodiscard]] IAssetLoader* LoaderForExtension(const std::filesystem::path& extension) const noexcept;
    [[nodiscard]] std::filesystem::path ResolvePhysicalPath(const AssetMetadata& metadata) const;
    [[nodiscard]] bool IsMountedVirtualPath(const std::filesystem::path& virtualPath) const;
    [[nodiscard]] static std::uint64_t HashFile(const std::filesystem::path& path) noexcept;
    AssetMountTable mounts_;
    AssetRegistry registry_;
    std::unique_ptr<AssetRegistrySnapshotCache> dependencySnapshots_;
    std::shared_ptr<bake::RuntimeAssetPack> runtimePack_;
    std::vector<AssetId> generatedAssets_;
    std::vector<std::unique_ptr<IAssetLoader>> loaders_;
    std::unordered_map<std::uint64_t, CachedAsset> cache_;
    mutable std::mutex loaderExecutionMutex_;
    std::atomic_uint64_t cancelledBeforeLoad_{0U};
    std::atomic_uint64_t cancelledDuringLoad_{0U};
    std::atomic_uint64_t discardedResults_{0U};
    std::atomic_uint64_t publishedLoads_{0U};
    std::unordered_map<std::uint64_t, AsyncLoadRecord> asyncLoads_;
    std::unordered_map<std::uint64_t, std::string> asyncLoadErrors_;
    std::unordered_map<std::uint64_t, std::uint64_t> asyncLoadGenerations_;
    std::unique_ptr<streaming::BackgroundLane> asyncLane_;
    std::uint32_t asyncConcurrency_ = 1U;
    std::atomic<std::uint64_t> serializedLoads_{ 0U };
    std::atomic<std::uint64_t> concurrentLoads_{ 0U };
    std::atomic<std::uint32_t> activeLoads_{ 0U };
    std::atomic<std::uint32_t> peakActiveLoads_{ 0U };
    mutable std::string lastError_;
    std::uint64_t revision_ = 1;
    mutable std::uint64_t cachedVirtualFoldersRevision_ = 0;
    mutable std::vector<std::filesystem::path> cachedVirtualFolders_;
};

} // namespace kb::assets
