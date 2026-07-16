#include "engine/assets/AssetManager.hpp"

#include "assets/AssetDiscoveryService.hpp"
#include "assets/AssetFileOperations.hpp"
#include "assets/AssetFileSystem.hpp"
#include "assets/AssetFolderOperations.hpp"
#include "assets/AssetLoaderRegistry.hpp"
#include "assets/AssetPathUtilities.hpp"
#include "assets/AssetRuntimeLoadService.hpp"

#include <utility>

namespace kb::assets {

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
    return AssetLoaderRegistry::Register(loaders_, std::move(loader));
}

bool AssetManager::RegisterAsset(AssetMetadata metadata) {
    if (metadata.id.IsValid()) {
        const bool changed = registry_.Upsert(std::move(metadata));
        if (changed) {
            ++revision_;
        }
        return changed;
    }
    if (metadata.type.empty() || metadata.virtualPath.empty()) {
        return false;
    }

    metadata.id = MakeAssetId(NormalizeAssetPath(metadata.virtualPath) + ":" + metadata.type);
    const bool changed = registry_.Upsert(std::move(metadata));
    if (changed) {
        ++revision_;
    }
    return changed;
}

std::size_t AssetManager::DiscoverMountedAssets() {
    const std::size_t count = AssetDiscoveryService::DiscoverMountedAssets(mounts_, registry_, loaders_, cache_);
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
    const bool created = AssetFolderOperations::CreateFolder(mounts_, virtualFolder, lastError_);
    if (created) {
        ++revision_;
    }
    return created;
}

std::optional<std::filesystem::path> AssetManager::CreateUniqueFolder(const std::filesystem::path& parentVirtualFolder, std::string baseName) {
    lastError_.clear();
    std::optional<std::filesystem::path> created = AssetFolderOperations::CreateUniqueFolder(mounts_, VirtualFolders(), parentVirtualFolder, std::move(baseName), lastError_);
    if (created.has_value()) {
        ++revision_;
    }
    return created;
}

bool AssetManager::RenameFolder(const std::filesystem::path& virtualFolder, std::string newName) {
    lastError_.clear();
    if (!AssetFolderOperations::RenameFolder(mounts_, virtualFolder, std::move(newName), lastError_)) {
        return false;
    }

    ClearRuntimeCache();
    static_cast<void>(DiscoverMountedAssets());
    return true;
}

bool AssetManager::DeleteFolder(const std::filesystem::path& virtualFolder) {
    lastError_.clear();
    if (!AssetFolderOperations::DeleteFolder(mounts_, virtualFolder, lastError_)) {
        return false;
    }

    ClearRuntimeCache();
    static_cast<void>(DiscoverMountedAssets());
    return true;
}

bool AssetManager::RenameAsset(AssetId id, std::string newName) {
    lastError_.clear();
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

bool AssetManager::Unload(AssetId id) noexcept {
    return cache_.erase(id.value) > 0;
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

std::string AssetManager::LastError() const {
    return lastError_;
}

void AssetManager::ClearRuntimeCache() noexcept {
    cache_.clear();
}

void AssetManager::Clear() noexcept {
    cache_.clear();
    registry_.Clear();
    mounts_.Clear();
    loaders_.clear();
    lastError_.clear();
    cachedVirtualFolders_.clear();
    cachedVirtualFoldersRevision_ = 0;
    ++revision_;
}

std::shared_ptr<void> AssetManager::LoadUntyped(AssetId id, std::type_index expectedType) {
    return AssetRuntimeLoadService::LoadUntyped(id, expectedType, registry_, mounts_, loaders_, cache_, lastError_);
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
