#include "assets/AssetDiscoveryService.hpp"

#include "assets/AssetFileSystem.hpp"
#include "assets/AssetLoaderRegistry.hpp"
#include "assets/AssetPathUtilities.hpp"
#include "engine/assets/ImportedAssetHeaderReader.hpp"
#include "engine/assets/bake/AssetPackReader.hpp"
#include "engine/assets/streaming/BackgroundLoadService.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <set>
#include <system_error>
#include <unordered_map>

namespace kb::assets {
namespace {

constexpr const char* kEditorLiveAssetOverrideCategory = "EditorLiveOverride";

[[nodiscard]] bool IsEditorLiveAssetOverride(const AssetMetadata& metadata) noexcept {
    return metadata.importCategory == kEditorLiveAssetOverrideCategory && metadata.runtimeLoadable;
}

// Which loader owns a baked package, asked of the PACKAGE rather than deduced from its
// extension. Every package ends in .kbpack whatever is inside it, so the extension says only
// "this is a package"; the artifact index says whether it holds textures or geometry.
//
// A package whose artifacts do not all load through one loader has no single asset type, and a
// discovery that picked one of them would put a type in the registry that the runtime then
// disagrees with -- the same silent divergence between catalogue and runtime that the prefab
// guid collision produced. It is left undiscovered instead, and so is a package this build
// cannot mount at all.
[[nodiscard]] IAssetLoader* LoaderForBakedPackage(
    const std::vector<std::unique_ptr<IAssetLoader>>& loaders,
    const std::filesystem::path& path) {
    kb::assets::bake::AssetPackReader reader;
    if (reader.Mount(path, kb::assets::bake::AssetPackAccess::Ranged) !=
        kb::assets::bake::AssetPackReadStatus::Success) {
        return nullptr;
    }
    // Current runtime loaders address a package as one asset and deliberately refuse a second
    // artifact of the same kind because metadata carries no digest/subasset selector. Discovery
    // must make the same decision or it registers an asset that can never load.
    if (reader.Artifacts().size() != 1U) {
        return nullptr;
    }
    IAssetLoader* owner = nullptr;
    for (const kb::assets::bake::AssetPackArtifactEntry& artifact : reader.Artifacts()) {
        IAssetLoader* match = nullptr;
        for (const std::unique_ptr<IAssetLoader>& candidate : loaders) {
            const std::vector<std::string> baked = candidate->BakedAssetTypes();
            if (std::ranges::find(baked, artifact.assetTypeId) == baked.end()) {
                continue;
            }
            // Type-id ownership has to be unique just like ordinary loader Type() ownership.
            // Taking the first claimant makes discovery depend on registration order and can
            // register a runtime type whose loader cannot actually decode this artifact.
            if (match != nullptr) {
                return nullptr;
            }
            match = candidate.get();
        }
        if (match == nullptr) {
            return nullptr;
        }
        if (owner != nullptr && owner != match) {
            return nullptr;
        }
        owner = match;
    }
    return owner;
}

// The content hashes of every file a loader claims, except baked packages (which may be gigabytes and are only hashed
// once a loader accepts them). The files are read by the load workers and by the calling thread together, which takes
// the next unhashed file whenever it is free, so it never waits for a worker that has not started.
[[nodiscard]] std::unordered_map<std::filesystem::path::string_type, std::uint64_t> HashClaimedFiles(
    const AssetMountTable& mounts,
    const std::vector<std::unique_ptr<IAssetLoader>>& loaders) {
    struct Batch {
        std::vector<std::filesystem::path> paths;
        std::vector<std::uint64_t> hashes;
        std::atomic<std::size_t> next{ 0U };
        std::atomic<std::size_t> done{ 0U };
        std::mutex mutex;
        std::condition_variable finished;
    };
    auto batch = std::make_shared<Batch>();
    for (const AssetMount& mount : mounts.Mounts()) {
        std::error_code error;
        if (!std::filesystem::exists(mount.root, error) || error) {
            continue;
        }
        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator{ mount.root, error }) {
            if (error || !entry.is_regular_file() ||
                AssetLoaderRegistry::FindByExtension(loaders, entry.path().extension()) == nullptr ||
                AssetPathUtilities::LowerExtension(entry.path().extension()) == kb::assets::bake::kAssetPackFileExtension) {
                continue;
            }
            batch->paths.push_back(entry.path());
        }
    }
    batch->hashes.assign(batch->paths.size(), 0U);
    const auto hashNext = [batch] {
        const std::size_t count = batch->paths.size();
        for (std::size_t index = batch->next.fetch_add(1U); index < count; index = batch->next.fetch_add(1U)) {
            batch->hashes[index] = AssetFileSystem::HashFileUnlessUnchanged(batch->paths[index]);
            if (batch->done.fetch_add(1U) + 1U == count) {
                std::scoped_lock lock{ batch->mutex };
                batch->finished.notify_all();
            }
        }
    };
    // A handful of files are quicker to read where they are asked for than to hand out.
    constexpr std::size_t kSpreadFrom = 16U;
    // Held until every file is hashed, so a service started only for this scan is not torn down under its helpers.
    std::shared_ptr<kb::assets::streaming::BackgroundLoadService> service;
    if (batch->paths.size() >= kSpreadFrom) {
        service = kb::assets::streaming::BackgroundLoadService::Shared();
        for (std::uint32_t helper = 0U; helper < service->JobWorkerLimit(); ++helper) {
            static_cast<void>(service->Run([hashNext](std::string&) {
                hashNext();
                return true;
            }, 0, kb::assets::streaming::BackgroundJobClass::Load));
        }
    }
    hashNext();
    {
        std::unique_lock lock{ batch->mutex };
        batch->finished.wait(lock, [&batch] { return batch->done.load() == batch->paths.size(); });
    }
    std::unordered_map<std::filesystem::path::string_type, std::uint64_t> hashes;
    hashes.reserve(batch->paths.size());
    for (std::size_t index = 0U; index < batch->paths.size(); ++index) {
        hashes.emplace(batch->paths[index].native(), batch->hashes[index]);
    }
    return hashes;
}

} // namespace

std::size_t AssetDiscoveryService::DiscoverMountedAssets(
    const AssetMountTable& mounts,
    AssetRegistry& registry,
    const std::vector<std::unique_ptr<IAssetLoader>>& loaders,
    std::unordered_map<std::uint64_t, AssetManager::CachedAsset>& cache) {
    std::size_t discovered = 0;
    std::set<std::uint64_t> discoveredIds;
    std::set<std::string> discoveredVirtualPaths;
    std::vector<AssetMetadata> previousAssets;
    previousAssets.assign(registry.All().begin(), registry.All().end());
    std::unordered_map<std::uint64_t, AssetMetadata> previousById;
    previousById.reserve(previousAssets.size());
    for (const AssetMetadata& metadata : previousAssets) {
        previousById.emplace(metadata.id.value, metadata);
    }

    const std::unordered_map<std::filesystem::path::string_type, std::uint64_t> fileHashes =
        HashClaimedFiles(mounts, loaders);
    for (const AssetMount& mount : mounts.Mounts()) {
        std::error_code error;
        if (!std::filesystem::exists(mount.root, error) || error) {
            continue;
        }

        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator{ mount.root, error }) {
            if (error || !entry.is_regular_file()) {
                continue;
            }

            IAssetLoader* loader = AssetLoaderRegistry::FindByExtension(loaders, entry.path().extension());
            if (loader == nullptr) {
                continue;
            }
            if (AssetPathUtilities::LowerExtension(entry.path().extension()) ==
                kb::assets::bake::kAssetPackFileExtension) {
                loader = LoaderForBakedPackage(loaders, entry.path());
                if (loader == nullptr) {
                    continue;
                }
            }

            std::optional<std::filesystem::path> virtualPath = mounts.ToVirtual(entry.path());
            if (!virtualPath.has_value()) {
                continue;
            }

            const auto hashed = fileHashes.find(entry.path().native());
            const std::uint64_t contentHash =
                hashed != fileHashes.end() ? hashed->second : AssetFileSystem::HashFileUnlessUnchanged(entry.path());
            std::string assetType{ loader->Type() };
            std::string importCategory;
            std::string browseTag;
            if (assetType == "ImportedAsset") {
                if (const std::optional<AssetImportCategory> category = ImportedAssetHeaderReader::ReadCategory(entry.path())) {
                    importCategory = std::string{ ToString(*category) };
                    assetType = std::string{ RuntimeAssetType(*category) };
                }
            } else {
                // DiscoverBrowseTag is only cheap relative to a full AssetManager::Load<T>() - for
                // loaders backed by a text/document format (e.g. ParticleEffectAssetLoader) it still
                // fully parses the file, so it is only re-run when the file's content actually
                // changed since the previous scan; an unchanged file reuses its previously discovered
                // tag instead of paying that parse again on every rescan (including the runtime's
                // periodically-throttled re-discovery, not just editor-triggered ones).
                const AssetId provisionalId = MakeAssetId(NormalizeAssetPath(*virtualPath) + ":" + assetType);
                const auto previousForTag = previousById.find(provisionalId.value);
                browseTag = (previousForTag != previousById.end() &&
                            previousForTag->second.contentHash == contentHash)
                    ? previousForTag->second.browseTag
                    : loader->DiscoverBrowseTag(entry.path());
            }

            const AssetId id = MakeAssetId(NormalizeAssetPath(*virtualPath) + ":" + assetType);
            AssetMetadata metadata{
                .id = id,
                .type = assetType,
                .importCategory = importCategory,
                .browseTag = browseTag,
                .name = entry.path().stem().string(),
                .virtualPath = *virtualPath,
                .physicalPath = entry.path(),
                .sourceExtension = AssetPathUtilities::LowerExtension(entry.path().extension()),
                .contentHash = contentHash,
                .dependencies = {},
                .runtimeLoadable = true,
            };
            const auto previous = previousById.find(id.value);
            if (previous != previousById.end() && IsEditorLiveAssetOverride(previous->second)) {
                discoveredVirtualPaths.insert(NormalizeAssetPath(*virtualPath));
                if (registry.Upsert(previous->second)) {
                    ++discovered;
                    discoveredIds.insert(id.value);
                }
                continue;
            }
            if (previous != previousById.end() &&
                (previous->second.contentHash != metadata.contentHash ||
                 previous->second.type != metadata.type ||
                 previous->second.importCategory != metadata.importCategory ||
                 NormalizeAssetPath(previous->second.virtualPath) != NormalizeAssetPath(metadata.virtualPath))) {
                static_cast<void>(cache.erase(id.value));
            }
            discoveredVirtualPaths.insert(NormalizeAssetPath(*virtualPath));
            if (registry.Upsert(std::move(metadata))) {
                ++discovered;
                discoveredIds.insert(id.value);
            }
        }
    }

    std::vector<AssetId> dependencyRefreshIds;
    dependencyRefreshIds.reserve(registry.All().size());
    for (const AssetMetadata& metadata : registry.All()) {
        dependencyRefreshIds.push_back(metadata.id);
    }
    for (const AssetId id : dependencyRefreshIds) {
        AssetMetadata* metadata = registry.FindMutable(id);
        if (metadata == nullptr || metadata->physicalPath.empty()) {
            continue;
        }
        IAssetLoader* loader = AssetLoaderRegistry::FindByExtension(loaders, metadata->physicalPath.extension());
        if (loader != nullptr && AssetPathUtilities::LowerExtension(metadata->physicalPath.extension()) ==
                kb::assets::bake::kAssetPackFileExtension) {
            loader = LoaderForBakedPackage(loaders, metadata->physicalPath);
        }
        if (loader == nullptr) {
            metadata->dependencies.clear();
            continue;
        }
        metadata->dependencies = loader->DiscoverDependencies(*metadata, registry);
    }

    for (const AssetMetadata& metadata : previousAssets) {
        const std::string normalizedVirtualPath = NormalizeAssetPath(metadata.virtualPath);
        if (!AssetPathUtilities::IsMountedVirtualPath(mounts, metadata.virtualPath) || discoveredIds.contains(metadata.id.value)) {
            continue;
        }

        const std::filesystem::path physical = AssetPathUtilities::ResolvePhysicalPath(mounts, metadata);
        std::error_code error;
        IAssetLoader* physicalLoader = physical.empty()
            ? nullptr
            : AssetLoaderRegistry::FindByExtension(loaders, physical.extension());
        if (physicalLoader != nullptr && AssetPathUtilities::LowerExtension(physical.extension()) ==
                kb::assets::bake::kAssetPackFileExtension) {
            physicalLoader = LoaderForBakedPackage(loaders, physical);
        }
        if (discoveredVirtualPaths.contains(normalizedVirtualPath)
            || physical.empty()
            || !std::filesystem::is_regular_file(physical, error)
            || physicalLoader == nullptr) {
            static_cast<void>(registry.Remove(metadata.id));
            static_cast<void>(cache.erase(metadata.id.value));
        }
    }
    return discovered;
}

std::vector<std::filesystem::path> AssetDiscoveryService::VirtualFolders(
    const AssetMountTable& mounts,
    const AssetRegistry& registry) {
    std::set<std::string> folders;
    for (const AssetMount& mount : mounts.Mounts()) {
        const std::filesystem::path rootVirtual{ "/" + mount.name };
        folders.insert(NormalizeAssetPath(rootVirtual));

        std::error_code error;
        if (!std::filesystem::exists(mount.root, error) || error) {
            continue;
        }

        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator{ mount.root, error }) {
            if (error || !entry.is_directory()) {
                continue;
            }
            if (std::optional<std::filesystem::path> virtualPath = mounts.ToVirtual(entry.path())) {
                folders.insert(NormalizeAssetPath(*virtualPath));
            }
        }
    }

    for (const AssetMetadata& metadata : registry.All()) {
        std::filesystem::path folder = AssetPathUtilities::ParentVirtualPath(metadata.virtualPath);
        while (!folder.empty()) {
            folders.insert(NormalizeAssetPath(folder));
            const std::filesystem::path parent = AssetPathUtilities::ParentVirtualPath(folder);
            if (parent == folder) {
                break;
            }
            folder = parent;
        }
    }

    std::vector<std::filesystem::path> output;
    output.reserve(folders.size());
    for (const std::string& folder : folders) {
        output.emplace_back(folder);
    }
    return output;
}

} // namespace kb::assets
