#pragma once

#include "engine/assets/AssetHandle.hpp"
#include "engine/assets/AssetManifest.hpp"
#include "engine/assets/AssetMountTable.hpp"
#include "engine/assets/IAssetLoader.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace kb::assets {

class AssetDiscoveryService;
class AssetRuntimeLoadService;

struct AssetMoveResult {
    bool succeeded = false;
    std::filesystem::path virtualPath{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return succeeded;
    }
};

class AssetManager {
public:
    [[nodiscard]] AssetMountTable& Mounts() noexcept;
    [[nodiscard]] const AssetMountTable& Mounts() const noexcept;
    [[nodiscard]] AssetRegistry& Registry() noexcept;
    [[nodiscard]] const AssetRegistry& Registry() const noexcept;
    [[nodiscard]] std::uint64_t Revision() const noexcept;

    [[nodiscard]] bool RegisterLoader(std::unique_ptr<IAssetLoader> loader);
    [[nodiscard]] bool RegisterAsset(AssetMetadata metadata);
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

    [[nodiscard]] bool Unload(AssetId id) noexcept;
    [[nodiscard]] bool IsLoaded(AssetId id) const noexcept;
    [[nodiscard]] std::size_t LoadedCount() const noexcept;
    [[nodiscard]] bool HasLoaderForType(std::string_view type) const noexcept;
    [[nodiscard]] std::string LastError() const;
    void SetError(std::string error) const;
    void ClearRuntimeCache() noexcept;
    void Clear() noexcept;

private:
    friend class AssetDiscoveryService;
    friend class AssetRuntimeLoadService;

    struct CachedAsset {
        std::shared_ptr<void> payload;
        std::type_index type = typeid(void);
    };

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
    std::vector<std::unique_ptr<IAssetLoader>> loaders_;
    std::unordered_map<std::uint64_t, CachedAsset> cache_;
    mutable std::string lastError_;
    std::uint64_t revision_ = 1;
    mutable std::uint64_t cachedVirtualFoldersRevision_ = 0;
    mutable std::vector<std::filesystem::path> cachedVirtualFolders_;
};

} // namespace kb::assets
