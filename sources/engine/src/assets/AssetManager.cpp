#include "engine/assets/AssetManager.hpp"

#include "engine/assets/bake/RuntimeAssetPack.hpp"

#include "assets/AssetDiscoveryService.hpp"
#include "assets/AssetFileOperations.hpp"
#include "assets/AssetFileSystem.hpp"
#include "assets/AssetFolderOperations.hpp"
#include "assets/AssetLoaderRegistry.hpp"
#include "assets/AssetPathUtilities.hpp"
#include "assets/AssetRuntimeLoadService.hpp"

#include <string>
#include <exception>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kb::assets {

AssetManager::~AssetManager() {
    StopAsyncWorker();
}

std::string_view ToString(AssetUnloadPolicy policy) noexcept {
    switch (policy) {
    case AssetUnloadPolicy::Retain:
        return "Retain";
    case AssetUnloadPolicy::ReleaseWhenUnreferenced:
        return "ReleaseWhenUnreferenced";
    }
    return "Retain";
}

bool TryParseAssetUnloadPolicy(std::string_view name, AssetUnloadPolicy& out) noexcept {
    if (name == "Retain") {
        out = AssetUnloadPolicy::Retain;
        return true;
    }
    if (name == "ReleaseWhenUnreferenced") {
        out = AssetUnloadPolicy::ReleaseWhenUnreferenced;
        return true;
    }
    return false;
}

AssetMountTable& AssetManager::Mounts() noexcept {
    return mounts_;
}

const AssetMountTable& AssetManager::Mounts() const noexcept {
    return mounts_;
}

AssetRegistry& AssetManager::Registry() noexcept {
    return registry_;
}

const AssetRegistry& AssetManager::Registry() const noexcept {
    return registry_;
}

std::uint64_t AssetManager::Revision() const noexcept {
    return revision_;
}

bool AssetManager::RegisterLoader(std::unique_ptr<IAssetLoader> loader) {
    if (loader == nullptr || loader->Type().empty()) {
        return false;
    }
    const bool replacesExistingType = HasLoaderForType(loader->Type());
    // A queued job stores the loader address. Finish the active job and
    // discard queued work before registration can mutate the loader registry.
    // Replacing an existing loader invalidates decoded payloads because their
    // producing code changed. Adding a genuinely new type cannot invalidate an
    // existing payload: publishing/loading that type was impossible without a
    // loader. Keeping unrelated retained assets resident is critical for
    // renderer plug-in discovery, which installs its loaders lazily after an
    // editor preview has already published engine-owned mesh/skeleton assets.
    StopAsyncWorker();
    if (replacesExistingType) {
        ClearRuntimeCache();
    }
    const bool registered = AssetLoaderRegistry::Register(loaders_, std::move(loader));
    if (registered) {
        RestartAsyncLoads();
    }
    return registered;
}

bool AssetManager::RegisterAsset(AssetMetadata metadata) {
    if (runtimePack_ != nullptr) {
        lastError_ = "Packaged runtime registry is immutable";
        return false;
    }
    if (metadata.id.IsValid()) {
        const AssetId id = metadata.id;
        const bool changed = registry_.Upsert(std::move(metadata));
        if (changed) {
            static_cast<void>(Unload(id));
            ++revision_;
        }
        return changed;
    }
    if (metadata.type.empty() || metadata.virtualPath.empty()) {
        return false;
    }

    metadata.id = MakeAssetId(NormalizeAssetPath(metadata.virtualPath) + ":" + metadata.type);
    const AssetId id = metadata.id;
    const bool changed = registry_.Upsert(std::move(metadata));
    if (changed) {
        static_cast<void>(Unload(id));
        ++revision_;
    }
    return changed;
}

bool AssetManager::MountRuntimePack(std::shared_ptr<bake::RuntimeAssetPack> pack) {
    lastError_.clear();
    if (pack == nullptr || !pack->IsMounted()) {
        lastError_ = "Runtime asset pack is not mounted";
        return false;
    }

    std::vector<AssetMetadata> packaged;
    packaged.reserve(pack->Manifest().assets.size());
    for (const bake::RuntimeAssetManifestEntry& entry : pack->Manifest().assets) {
        if (!entry.id.IsValid() || entry.type.empty() || entry.virtualPath.empty()) {
            lastError_ = "Runtime asset manifest contains an invalid registry entry";
            return false;
        }
        packaged.push_back(AssetMetadata{
            .id = entry.id,
            .type = entry.type,
            .importCategory = entry.importCategory,
            .browseTag = entry.browseTag,
            .name = entry.name,
            .virtualPath = entry.virtualPath,
            .physicalPath = {},
            .sourceExtension = entry.sourceExtension,
            .contentHash = entry.contentHash,
            .dependencies = entry.dependencies,
            .runtimeLoadable = entry.runtimeLoadable,
        });
    }

    StopAsyncWorker();
    asyncLoads_.clear();
    asyncLoadErrors_.clear();
    asyncLoadGenerations_.clear();
    cache_.clear();
    registry_.Clear();
    mounts_.Clear();
    for (AssetMetadata& metadata : packaged) {
        if (!registry_.Upsert(std::move(metadata))) {
            registry_.Clear();
            lastError_ = "Runtime asset manifest could not populate the registry";
            return false;
        }
    }
    runtimePack_ = std::move(pack);
    cachedVirtualFolders_.clear();
    cachedVirtualFoldersRevision_ = 0U;
    ++revision_;
    RestartAsyncLoads();
    return true;
}

bool AssetManager::IsRuntimePackMounted() const noexcept {
    return runtimePack_ != nullptr;
}

std::shared_ptr<bake::RuntimeAssetPack> AssetManager::RuntimePack() const noexcept {
    return runtimePack_;
}

bool AssetManager::RefreshAsset(AssetId id) {
    lastError_.clear();
    if (runtimePack_ != nullptr) {
        lastError_ = "Packaged runtime assets are immutable";
        return false;
    }
    const AssetMetadata* registered = registry_.Find(id);
    if (!id.IsValid() || registered == nullptr) {
        lastError_ = "Asset is not registered";
        return false;
    }

    AssetMetadata refreshed = *registered;
    refreshed.physicalPath = ResolvePhysicalPath(refreshed);
    std::error_code fileError;
    if (refreshed.physicalPath.empty() ||
        !std::filesystem::is_regular_file(refreshed.physicalPath, fileError) ||
        fileError) {
        lastError_ = "Asset file is unavailable";
        return false;
    }

    IAssetLoader* loader = LoaderForExtension(refreshed.physicalPath.extension());
    if (loader == nullptr) {
        lastError_ = "No loader registered for asset extension: " +
            refreshed.physicalPath.extension().string();
        return false;
    }

    const std::uint64_t contentHash = HashFile(refreshed.physicalPath);
    if (contentHash == 0U) {
        lastError_ = "Asset file could not be hashed";
        return false;
    }

    try {
        std::scoped_lock lock{ loaderExecutionMutex_ };
        if (loader->Type() == refreshed.type) {
            refreshed.browseTag = loader->DiscoverBrowseTag(refreshed.physicalPath);
        }
        refreshed.contentHash = contentHash;
        refreshed.dependencies = loader->DiscoverDependencies(refreshed, registry_);
    } catch (const std::exception& exception) {
        lastError_ = "Asset metadata refresh failed: " +
            std::string{ exception.what() };
        return false;
    } catch (...) {
        lastError_ = "Asset metadata refresh failed";
        return false;
    }

    std::unordered_set<std::uint64_t> invalidatedAssets{ id.value };
    bool foundDependent = true;
    while (foundDependent) {
        foundDependent = false;
        for (const AssetMetadata& metadata : registry_.All()) {
            for (const AssetId dependency : metadata.dependencies) {
                if (invalidatedAssets.contains(dependency.value) &&
                    invalidatedAssets.insert(metadata.id.value).second) {
                    foundDependent = true;
                    break;
                }
            }
        }
    }

    if (!registry_.Upsert(std::move(refreshed))) {
        lastError_ = "Asset registry could not be refreshed";
        return false;
    }
    for (const std::uint64_t invalidatedId : invalidatedAssets) {
        ++asyncLoadGenerations_[invalidatedId];
        asyncLoads_.erase(invalidatedId);
        asyncLoadErrors_.erase(invalidatedId);
        cache_.erase(invalidatedId);
    }
    ++revision_;
    return true;
}

std::size_t AssetManager::DiscoverMountedAssets() {
    if (runtimePack_ != nullptr) {
        lastError_ = "Packaged runtime assets are indexed by their manifest";
        return 0U;
    }
    // Discovery calls loader dependency scanners and may replace registry
    // metadata. Do not race either operation with queued loader calls, and
    // never publish a payload decoded from pre-discovery metadata.
    StopAsyncWorker();
    std::unordered_map<std::uint64_t, std::uint64_t> previousContentHashes;
    previousContentHashes.reserve(registry_.All().size());
    for (const AssetMetadata& metadata : registry_.All()) {
        previousContentHashes.emplace(metadata.id.value, metadata.contentHash);
    }
    for (const auto& [assetId, record] : asyncLoads_) {
        static_cast<void>(record);
        ++asyncLoadGenerations_[assetId];
    }
    asyncLoads_.clear();
    const std::size_t count = AssetDiscoveryService::DiscoverMountedAssets(mounts_, registry_, loaders_, cache_);
    std::unordered_set<std::uint64_t> invalidatedAssets;
    invalidatedAssets.reserve(previousContentHashes.size());
    for (const auto& [assetId, previousContentHash] : previousContentHashes) {
        const AssetMetadata* current = registry_.Find(AssetId{ assetId });
        if (current == nullptr || current->contentHash != previousContentHash) {
            invalidatedAssets.insert(assetId);
        }
    }

    // A skeletal mesh/clip/controller may keep identical source bytes while a
    // referenced Skeleton or Clip has changed. Invalidate the complete
    // dependent closure so derived runtime bindings cannot continue sampling
    // an obsolete asset payload after discovery.
    bool foundDependent = true;
    while (foundDependent) {
        foundDependent = false;
        for (const AssetMetadata& metadata : registry_.All()) {
            for (const AssetId dependency : metadata.dependencies) {
                if (invalidatedAssets.contains(dependency.value) &&
                    invalidatedAssets.insert(metadata.id.value).second) {
                    foundDependent = true;
                    break;
                }
            }
        }
    }
    for (const std::uint64_t assetId : invalidatedAssets) {
        ++asyncLoadGenerations_[assetId];
        cache_.erase(assetId);
        asyncLoadErrors_.erase(assetId);
    }
    ++revision_;
    return count;
}

std::vector<std::filesystem::path> AssetManager::VirtualFolders() const {
    if (cachedVirtualFoldersRevision_ != revision_) {
        cachedVirtualFolders_ = AssetDiscoveryService::VirtualFolders(mounts_, registry_);
        cachedVirtualFoldersRevision_ = revision_;
    }
    return cachedVirtualFolders_;
}

bool AssetManager::CreateFolder(const std::filesystem::path& virtualFolder) {
    lastError_.clear();
    if (runtimePack_ != nullptr) {
        lastError_ = "Packaged runtime assets are immutable";
        return false;
    }
    const bool created = AssetFolderOperations::CreateFolder(mounts_, virtualFolder, lastError_);
    if (created) {
        ++revision_;
    }
    return created;
}

std::optional<std::filesystem::path> AssetManager::CreateUniqueFolder(const std::filesystem::path& parentVirtualFolder, std::string baseName) {
    lastError_.clear();
    if (runtimePack_ != nullptr) {
        lastError_ = "Packaged runtime assets are immutable";
        return std::nullopt;
    }
    std::optional<std::filesystem::path> created = AssetFolderOperations::CreateUniqueFolder(mounts_, VirtualFolders(), parentVirtualFolder, std::move(baseName), lastError_);
    if (created.has_value()) {
        ++revision_;
    }
    return created;
}

bool AssetManager::RenameFolder(const std::filesystem::path& virtualFolder, std::string newName) {
    lastError_.clear();
    if (runtimePack_ != nullptr) {
        lastError_ = "Packaged runtime assets are immutable";
        return false;
    }
    if (!AssetFolderOperations::RenameFolder(mounts_, virtualFolder, std::move(newName), lastError_)) {
        return false;
    }

    ClearRuntimeCache();
    static_cast<void>(DiscoverMountedAssets());
    return true;
}

bool AssetManager::DeleteFolder(const std::filesystem::path& virtualFolder) {
    lastError_.clear();
    if (runtimePack_ != nullptr) {
        lastError_ = "Packaged runtime assets are immutable";
        return false;
    }
    if (!AssetFolderOperations::DeleteFolder(mounts_, virtualFolder, lastError_)) {
        return false;
    }

    ClearRuntimeCache();
    static_cast<void>(DiscoverMountedAssets());
    return true;
}

bool AssetManager::RenameAsset(AssetId id, std::string newName) {
    lastError_.clear();
    if (runtimePack_ != nullptr) {
        lastError_ = "Packaged runtime assets are immutable";
        return false;
    }
    const AssetMetadata* existing = registry_.Find(id);
    if (existing == nullptr) {
        lastError_ = "Asset is not registered";
        return false;
    }

    AssetMetadata renamed = *existing;
    const std::filesystem::path renamedVirtualPath = renamed.virtualPath.parent_path() / (newName + renamed.virtualPath.extension().string());
    if (!AssetFileOperations::RenameAsset(registry_, mounts_, id, std::move(newName), lastError_)) {
        return false;
    }

    static_cast<void>(Unload(id));
    renamed.name = renamedVirtualPath.stem().string();
    renamed.virtualPath = renamedVirtualPath;
    renamed.physicalPath = mounts_.Resolve(renamedVirtualPath).value_or(std::filesystem::path{});
    renamed.contentHash = renamed.physicalPath.empty() ? renamed.contentHash : HashFile(renamed.physicalPath);
    if (!registry_.Upsert(std::move(renamed))) {
        lastError_ = "Asset registry could not be updated after rename";
        return false;
    }
    ++revision_;
    return true;
}

AssetMoveResult AssetManager::MoveAssetIntoFolder(AssetId id, const std::filesystem::path& destinationVirtualFolder) {
    lastError_.clear();
    if (runtimePack_ != nullptr) {
        lastError_ = "Packaged runtime assets are immutable";
        return {};
    }
    const AssetMetadata* metadata = registry_.Find(id);
    const std::filesystem::path previousVirtualPath = metadata == nullptr ? std::filesystem::path{} : metadata->virtualPath;
    const AssetMoveResult moved = AssetFileOperations::MoveAssetIntoFolder(registry_, mounts_, id, destinationVirtualFolder, lastError_);
    if (!moved.succeeded) {
        return moved;
    }

    if (NormalizeAssetPath(previousVirtualPath) != NormalizeAssetPath(moved.virtualPath)) {
        static_cast<void>(Unload(id));
        static_cast<void>(DiscoverMountedAssets());
    }
    return moved;
}

bool AssetManager::MoveAsset(AssetId id, const std::filesystem::path& destinationVirtualFolder) {
    return MoveAssetIntoFolder(id, destinationVirtualFolder).succeeded;
}

AssetMoveResult AssetManager::MoveFolderIntoFolder(const std::filesystem::path& sourceVirtualFolder, const std::filesystem::path& destinationVirtualFolder) {
    lastError_.clear();
    if (runtimePack_ != nullptr) {
        lastError_ = "Packaged runtime assets are immutable";
        return {};
    }
    const AssetMoveResult moved = AssetFolderOperations::MoveFolderIntoFolder(mounts_, sourceVirtualFolder, destinationVirtualFolder, lastError_);
    if (!moved.succeeded) {
        return moved;
    }

    if (NormalizeAssetPath(sourceVirtualFolder) != NormalizeAssetPath(moved.virtualPath)) {
        ClearRuntimeCache();
        static_cast<void>(DiscoverMountedAssets());
    }
    return moved;
}

bool AssetManager::MoveFolder(const std::filesystem::path& sourceVirtualFolder, const std::filesystem::path& destinationVirtualFolder) {
    return MoveFolderIntoFolder(sourceVirtualFolder, destinationVirtualFolder).succeeded;
}

bool AssetManager::DeleteAsset(AssetId id) {
    lastError_.clear();
    if (runtimePack_ != nullptr) {
        lastError_ = "Packaged runtime assets are immutable";
        return false;
    }
    if (!AssetFileOperations::DeleteAsset(registry_, mounts_, id, lastError_)) {
        return false;
    }

    static_cast<void>(Unload(id));
    if (registry_.Remove(id)) {
        ++revision_;
    }
    return true;
}

bool AssetManager::LoadOpaque(AssetId id) {
    lastError_.clear();
    if (!id.IsValid()) {
        lastError_ = "Invalid asset id";
        return false;
    }

    const AssetMetadata* metadata = registry_.Find(id);
    if (metadata == nullptr) {
        lastError_ = "Asset is not registered";
        return false;
    }

    IAssetLoader* loader = LoaderForType(metadata->type);
    if (loader == nullptr) {
        lastError_ = "No loader registered for asset type: " + metadata->type;
        return false;
    }

    return LoadUntyped(id, loader->PayloadType()) != nullptr;
}

AssetOpaqueHandle AssetManager::LoadOpaqueHandle(AssetId id) {
    lastError_.clear();
    if (!id.IsValid()) {
        lastError_ = "Invalid asset id";
        return {};
    }
    const AssetMetadata* metadata = registry_.Find(id);
    IAssetLoader* loader = metadata == nullptr ? nullptr : LoaderForType(metadata->type);
    if (loader == nullptr) {
        lastError_ = metadata == nullptr ? "Asset is not registered" : "No loader registered for asset type: " + metadata->type;
        return {};
    }
    std::shared_ptr<void> payload = LoadUntyped(id, loader->PayloadType());
    return payload == nullptr ? AssetOpaqueHandle{} : AssetOpaqueHandle{ id, std::move(payload) };
}

AssetOpaqueHandle AssetManager::AcquireOpaqueHandle(AssetId id) const {
    const auto cached = cache_.find(id.value);
    if (cached == cache_.end()) {
        return {};
    }
    std::shared_ptr<void> payload = cached->second.weak.lock();
    return payload == nullptr ? AssetOpaqueHandle{} : AssetOpaqueHandle{ id, std::move(payload) };
}

bool AssetManager::RequestLoadAsync(AssetId id, AssetUnloadPolicy policy) {
    lastError_.clear();
    if (!id.IsValid()) {
        lastError_ = "Invalid asset id";
        return false;
    }
    if (IsLoaded(id)) {
        asyncLoadErrors_.erase(id.value);
        return true;
    }
    if (asyncLoads_.contains(id.value)) {
        return true;
    }
    const AssetMetadata* registered = registry_.Find(id);
    if (registered == nullptr || !registered->runtimeLoadable) {
        lastError_ = registered == nullptr ? "Asset is not registered" : "Asset is not runtime loadable";
        asyncLoadErrors_[id.value] = lastError_;
        return true;
    }
    IAssetLoader* loader = LoaderForType(registered->type);
    if (loader == nullptr) {
        lastError_ = "No loader registered for asset type: " + registered->type;
        asyncLoadErrors_[id.value] = lastError_;
        return true;
    }
    const std::filesystem::path resolvedPath = ResolvePhysicalPath(*registered);
    if (resolvedPath.empty() && runtimePack_ == nullptr) {
        lastError_ = "Asset path could not be resolved: " + NormalizeAssetPath(registered->virtualPath);
        asyncLoadErrors_[id.value] = lastError_;
        return true;
    }
    const AssetLoadRequest request{
        .metadata = *registered,
        .resolvedPath = resolvedPath,
        .runtimePack = runtimePack_,
    };
    if (const std::optional<std::string> diagnostic =
            loader->ValidateRuntimeDependencies(request, registry_);
        diagnostic.has_value()) {
        lastError_ = "Asset dependency validation failed: " + *diagnostic;
        asyncLoadErrors_[id.value] = lastError_;
        return true;
    }

    const std::uint64_t generation = asyncLoadGenerations_[id.value];
    const std::string payloadTypeName = loader->PayloadType().name();
    auto state = std::make_shared<AsyncPreparedState>();
    asyncLoadErrors_.erase(id.value);
    StartAsyncWorker();
    asyncLoads_.emplace(id.value, AsyncLoadRecord{
        .state = state,
        .policy = policy,
        .generation = generation,
    });
    try {
        {
            std::scoped_lock lock{ asyncWorkerMutex_ };
            asyncWorkerQueue_.push_back(AsyncLoadJob{
                .loader = loader,
                .metadata = *registered,
                .resolvedPath = resolvedPath,
                .runtimePack = runtimePack_,
                .typeName = payloadTypeName,
                .state = std::move(state),
            });
        }
        asyncWorkerWake_.notify_one();
    } catch (...) {
        asyncLoads_.erase(id.value);
        throw;
    }
    return true;
}

void AssetManager::StartAsyncWorker() {
    std::scoped_lock lock{ asyncWorkerMutex_ };
    if (asyncWorker_.joinable()) {
        return;
    }
    asyncWorkerStopping_ = false;
    asyncWorker_ = std::thread{ [this] {
        RunAsyncWorker();
    } };
}

void AssetManager::StopAsyncWorker() noexcept {
    {
        std::scoped_lock lock{ asyncWorkerMutex_ };
        asyncWorkerStopping_ = true;
        asyncWorkerQueue_.clear();
    }
    asyncWorkerWake_.notify_one();
    if (asyncWorker_.joinable()) {
        asyncWorker_.join();
    }
    asyncWorkerStopping_ = false;
}

void AssetManager::RestartAsyncLoads() {
    auto pendingLoads = std::move(asyncLoads_);
    for (auto& [assetValue, record] : pendingLoads) {
        const AssetId id{ assetValue };
        const AssetMetadata* metadata = registry_.Find(id);
        IAssetLoader* loader = metadata == nullptr ? nullptr : LoaderForType(metadata->type);
        if (metadata == nullptr || loader == nullptr) {
            asyncLoadErrors_[assetValue] = metadata == nullptr
                ? "Asset is not registered"
                : "No loader registered for asset type: " + metadata->type;
            continue;
        }

        const std::filesystem::path resolvedPath = ResolvePhysicalPath(*metadata);
        if (resolvedPath.empty() && runtimePack_ == nullptr) {
            asyncLoadErrors_[assetValue] = "Asset path is not mounted";
            continue;
        }

        record.state = std::make_shared<AsyncPreparedState>();
        // ClearRuntimeCache invalidates the old worker result by advancing
        // this generation. The replacement job is the same caller request,
        // now bound to the newly registered loader, so it must publish under
        // the current generation.
        record.generation = LoadGeneration(id);
        const std::string typeName = loader->PayloadType().name();
        const std::shared_ptr<AsyncPreparedState> state = record.state;
        asyncLoads_.emplace(assetValue, std::move(record));
        StartAsyncWorker();
        try {
            std::scoped_lock lock{ asyncWorkerMutex_ };
            asyncWorkerQueue_.push_back(AsyncLoadJob{
                .loader = loader,
                .metadata = *metadata,
                .resolvedPath = resolvedPath,
                .runtimePack = runtimePack_,
                .typeName = typeName,
                .state = state,
            });
        } catch (...) {
            asyncLoads_.erase(assetValue);
            throw;
        }
        asyncWorkerWake_.notify_one();
    }
}

void AssetManager::RunAsyncWorker() noexcept {
    while (true) {
        AsyncLoadJob job;
        {
            std::unique_lock lock{ asyncWorkerMutex_ };
            asyncWorkerWake_.wait(lock, [this] {
                return asyncWorkerStopping_ || !asyncWorkerQueue_.empty();
            });
            if (asyncWorkerStopping_) {
                return;
            }
            job = std::move(asyncWorkerQueue_.front());
            asyncWorkerQueue_.pop_front();
        }

        AsyncPreparedAsset prepared;
        try {
            std::scoped_lock lock{ loaderExecutionMutex_ };
            prepared = AsyncPreparedAsset{
                .result = job.loader->Load(AssetLoadRequest{
                    .metadata = job.metadata,
                    .resolvedPath = job.resolvedPath,
                    .runtimePack = std::move(job.runtimePack),
                }),
                .typeName = job.typeName,
            };
        } catch (const std::exception& exception) {
            prepared = AsyncPreparedAsset{
                .result = AssetLoadResult{ .asset = {}, .error = "Asset loader threw an exception: " + std::string{ exception.what() } },
                .typeName = job.typeName,
            };
        } catch (...) {
            prepared = AsyncPreparedAsset{
                .result = AssetLoadResult{ .asset = {}, .error = "Asset loader threw a non-standard exception" },
                .typeName = job.typeName,
            };
        }
        std::scoped_lock stateLock{ job.state->mutex };
        job.state->prepared = std::move(prepared);
    }
}

void AssetManager::PumpAsyncLoads() {
    for (auto iterator = asyncLoads_.begin(); iterator != asyncLoads_.end();) {
        std::optional<AsyncPreparedAsset> prepared;
        {
            std::scoped_lock stateLock{ iterator->second.state->mutex };
            if (iterator->second.state->prepared.has_value()) {
                prepared = std::move(iterator->second.state->prepared);
            }
        }
        if (!prepared.has_value()) {
            ++iterator;
            continue;
        }
        const std::uint64_t assetId = iterator->first;
        const AssetUnloadPolicy policy = iterator->second.policy;
        const std::uint64_t generation = iterator->second.generation;
        iterator = asyncLoads_.erase(iterator);
        if (asyncLoadGenerations_[assetId] != generation) {
            continue;
        }
        if (!prepared->result.Succeeded()) {
            asyncLoadErrors_[assetId] = prepared->result.error.empty() ? "Asset loader failed" : std::move(prepared->result.error);
            continue;
        }
        cache_[assetId] = CachedAsset{
            .retained = policy == AssetUnloadPolicy::Retain ? prepared->result.asset : std::shared_ptr<void>{},
            .weak = prepared->result.asset,
            .typeName = prepared->typeName,
            .policy = policy,
        };
        asyncLoadErrors_.erase(assetId);
    }
}

AsyncAssetLoadStatus AssetManager::AsyncLoadStatus(AssetId id) const noexcept {
    if (IsLoaded(id)) {
        return AsyncAssetLoadStatus::Completed;
    }
    if (asyncLoads_.contains(id.value)) {
        return AsyncAssetLoadStatus::Pending;
    }
    return asyncLoadErrors_.contains(id.value) ? AsyncAssetLoadStatus::Failed : AsyncAssetLoadStatus::NotRequested;
}

std::string AssetManager::AsyncLoadError(AssetId id) const {
    const auto iterator = asyncLoadErrors_.find(id.value);
    return iterator == asyncLoadErrors_.end() ? std::string{} : iterator->second;
}

bool AssetManager::Unload(AssetId id) noexcept {
    ++asyncLoadGenerations_[id.value];
    asyncLoadErrors_.erase(id.value);
    return cache_.erase(id.value) > 0;
}

std::uint64_t AssetManager::LoadGeneration(AssetId id) const noexcept {
    const auto generation = asyncLoadGenerations_.find(id.value);
    return generation == asyncLoadGenerations_.end() ? 0U : generation->second;
}

bool AssetManager::IsLoaded(AssetId id) const noexcept {
    const auto cached = cache_.find(id.value);
    return cached != cache_.end() && !cached->second.weak.expired();
}

std::size_t AssetManager::LoadedCount() const noexcept {
    std::size_t count = 0;
    for (const auto& [key, entry] : cache_) {
        if (!entry.weak.expired()) {
            ++count;
        }
    }
    return count;
}

std::size_t AssetManager::ReferenceCount(AssetId id) const noexcept {
    const auto cached = cache_.find(id.value);
    if (cached == cache_.end()) {
        return 0;
    }
    const long total = cached->second.weak.use_count();
    const long cacheOwned = cached->second.retained != nullptr ? 1 : 0;
    const long external = total - cacheOwned;
    return external <= 0 ? 0 : static_cast<std::size_t>(external);
}

AssetUnloadPolicy AssetManager::UnloadPolicy(AssetId id) const noexcept {
    const auto cached = cache_.find(id.value);
    return cached == cache_.end() ? AssetUnloadPolicy::Retain : cached->second.policy;
}

bool AssetManager::SetUnloadPolicy(AssetId id, AssetUnloadPolicy policy) {
    const auto cached = cache_.find(id.value);
    if (cached == cache_.end() || cached->second.weak.expired()) {
        return false;
    }
    cached->second.policy = policy;
    if (policy == AssetUnloadPolicy::ReleaseWhenUnreferenced) {
        // Drop the cache's strong reference; the payload now lives only as
        // long as some external AssetHandle holds it. If none does, it is
        // freed here and the dead entry pruned immediately.
        cached->second.retained.reset();
        if (cached->second.weak.expired()) {
            cache_.erase(cached);
        }
    } else {
        // Re-pin the still-live payload so the cache keeps it resident.
        cached->second.retained = cached->second.weak.lock();
    }
    return true;
}

std::size_t AssetManager::PruneUnreferenced() noexcept {
    std::size_t removed = 0;
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (it->second.weak.expired()) {
            it = cache_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

bool AssetManager::HasLoaderForType(std::string_view type) const noexcept {
    return LoaderForType(type) != nullptr;
}

namespace {

[[nodiscard]] std::string DescribeAsset(const AssetMetadata& metadata) {
    const std::string virtualPath = NormalizeAssetPath(metadata.virtualPath);
    return (virtualPath.empty() ? ToString(metadata.id) : virtualPath) + " (" + ToString(metadata.id) + ")";
}

} // namespace

AssetCompatibilityReport AssetManager::ValidateCompatibility(AssetId id) const {
    AssetCompatibilityReport report;

    const AssetMetadata* root = registry_.Find(id);
    if (root == nullptr) {
        report.diagnostics.push_back(AssetCompatibilityDiagnostic{
            .issue = AssetCompatibilityIssue::MissingDependency,
            .asset = id,
            .dependency = id,
            .message = "Asset " + ToString(id) + " is not registered",
        });
        report.compatible = false;
        return report;
    }

    std::unordered_set<std::uint64_t> visited;
    std::vector<AssetId> pending{ id };
    while (!pending.empty()) {
        const AssetId current = pending.back();
        pending.pop_back();
        if (!visited.insert(current.value).second) {
            continue;
        }

        const AssetMetadata* metadata = registry_.Find(current);
        if (metadata == nullptr) {
            // Only ids we already confirmed registered are pushed, so this
            // path is unreachable in practice; guard defensively.
            continue;
        }

        IAssetLoader* loader = LoaderForType(metadata->type);
        if (loader == nullptr) {
            report.diagnostics.push_back(AssetCompatibilityDiagnostic{
                .issue = AssetCompatibilityIssue::IncompatibleType,
                .asset = current,
                .dependency = {},
                .message = "Asset " + DescribeAsset(*metadata) + " has type \"" + metadata->type + "\" which has no registered loader in this runtime",
            });
        } else if (const std::optional<std::string> diagnostic = [&]() {
                       const AssetLoadRequest request{
                           .metadata = *metadata,
                           .resolvedPath = ResolvePhysicalPath(*metadata),
                           .runtimePack = runtimePack_,
                       };
                       return loader->ValidateRuntimeDependencies(request, registry_);
                   }();
                   diagnostic.has_value()) {
            report.diagnostics.push_back(AssetCompatibilityDiagnostic{
                .issue = AssetCompatibilityIssue::IncompatibleDependency,
                .asset = current,
                .dependency = {},
                .message = "Asset " + DescribeAsset(*metadata) + " " + *diagnostic,
            });
        }

        for (const AssetId dependency : metadata->dependencies) {
            const AssetMetadata* dependencyMetadata = registry_.Find(dependency);
            if (dependencyMetadata == nullptr) {
                report.diagnostics.push_back(AssetCompatibilityDiagnostic{
                    .issue = AssetCompatibilityIssue::MissingDependency,
                    .asset = current,
                    .dependency = dependency,
                    .message = "Asset " + DescribeAsset(*metadata) + " depends on " + ToString(dependency) + " which is not registered",
                });
                continue;
            }
            pending.push_back(dependency);
        }
    }

    report.compatible = report.diagnostics.empty();
    return report;
}

bool AssetManager::IsCompatible(AssetId id) const {
    return ValidateCompatibility(id).compatible;
}

std::string AssetManager::LastError() const {
    return lastError_;
}

void AssetManager::ClearRuntimeCache() noexcept {
    cache_.clear();
    asyncLoadErrors_.clear();
}

void AssetManager::Clear() noexcept {
    StopAsyncWorker();
    asyncLoads_.clear();
    asyncLoadErrors_.clear();
    asyncLoadGenerations_.clear();
    cache_.clear();
    registry_.Clear();
    mounts_.Clear();
    runtimePack_.reset();
    loaders_.clear();
    lastError_.clear();
    cachedVirtualFolders_.clear();
    cachedVirtualFoldersRevision_ = 0;
    ++revision_;
}

std::shared_ptr<void> AssetManager::LoadUntyped(AssetId id, std::type_index expectedType) {
    return AssetRuntimeLoadService::LoadUntyped(
        id,
        expectedType,
        registry_,
        mounts_,
        loaders_,
        runtimePack_,
        cache_,
        loaderExecutionMutex_,
        lastError_);
}

IAssetLoader* AssetManager::LoaderForType(std::string_view type) const noexcept {
    return AssetLoaderRegistry::FindByType(loaders_, type);
}

IAssetLoader* AssetManager::LoaderForExtension(const std::filesystem::path& extension) const noexcept {
    return AssetLoaderRegistry::FindByExtension(loaders_, extension);
}

std::filesystem::path AssetManager::ResolvePhysicalPath(const AssetMetadata& metadata) const {
    return AssetPathUtilities::ResolvePhysicalPath(mounts_, metadata);
}

bool AssetManager::IsMountedVirtualPath(const std::filesystem::path& virtualPath) const {
    return AssetPathUtilities::IsMountedVirtualPath(mounts_, virtualPath);
}

std::uint64_t AssetManager::HashFile(const std::filesystem::path& path) noexcept {
    return AssetFileSystem::HashFile(path);
}

void AssetManager::SetError(std::string error) const {
    lastError_ = std::move(error);
}

} // namespace kb::assets
