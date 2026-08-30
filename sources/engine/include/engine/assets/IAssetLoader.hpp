#pragma once

#include "engine/assets/AssetMetadata.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <span>
#include <vector>

namespace kb::assets {

class AssetRegistry;
namespace bake {
class RuntimeAssetPack;
enum class RuntimeArtifactEncoding : std::uint8_t;
struct RuntimeAssetPayload;
}

struct AssetLoadRequest {
    const AssetMetadata& metadata;
    std::filesystem::path resolvedPath;
    std::shared_ptr<bake::RuntimeAssetPack> runtimePack;

    [[nodiscard]] bool IsPackaged() const noexcept {
        return runtimePack != nullptr;
    }

    [[nodiscard]] std::string SourceExtension() const {
        return metadata.sourceExtension.empty()
            ? resolvedPath.extension().string()
            : metadata.sourceExtension;
    }

    // Reads the authored representation in either mode. Packaged mode verifies both the
    // container payload digest and the manifest content hash before returning raw source bytes;
    // loose mode reads resolvedPath. Output changes only on success.
    [[nodiscard]] bool ReadSourceBytes(std::vector<std::uint8_t>& out, std::string& error) const;

    // Reads another registered asset through the same loose/package source as this request.
    // Dependency validators use this instead of opening AssetMetadata::physicalPath, which is
    // intentionally empty for immutable runtime-package manifests.
    [[nodiscard]] bool ReadDependencySourceBytes(
        const AssetMetadata& dependency,
        std::vector<std::uint8_t>& out,
        std::string& error) const;

    // Explicit cooked-data access. A loader must name the encoding and qualifier it expects;
    // the pack never guesses among texture families or shader variants.
    [[nodiscard]] bool ReadPackagedPayload(
        bake::RuntimeArtifactEncoding encoding,
        std::string_view qualifier,
        bake::RuntimeAssetPayload& out,
        std::string& error) const;
};

struct AssetLoadResult {
    std::shared_ptr<void> asset{};
    std::string error;

    [[nodiscard]] bool Succeeded() const noexcept {
        return asset != nullptr && error.empty();
    }
};

class IAssetLoader {
public:
    virtual ~IAssetLoader() = default;

    [[nodiscard]] virtual std::string_view Type() const noexcept = 0;
    [[nodiscard]] virtual std::type_index PayloadType() const noexcept = 0;
    [[nodiscard]] virtual std::vector<std::string> Extensions() const = 0;
    // Baked artifact type ids (BakedAssetDescriptor::assetTypeId) this loader can load out of
    // an asset package. A package carries its contents' type in its own index, so discovery
    // asks the file which loader owns it instead of reading that off the shared .kbpack
    // extension -- which would hand every package to whichever loader happened to claim the
    // extension first. Empty for a loader with no baked form.
    [[nodiscard]] virtual std::vector<std::string> BakedAssetTypes() const {
        return {};
    }
    [[nodiscard]] virtual AssetLoadResult Load(const AssetLoadRequest& request) = 0;
    [[nodiscard]] virtual std::vector<AssetId> DiscoverDependencies(const AssetMetadata& metadata, const AssetRegistry& registry) const {
        static_cast<void>(metadata);
        static_cast<void>(registry);
        return {};
    }
    [[nodiscard]] virtual std::optional<std::string> ValidateDependencies(
        const AssetMetadata& metadata,
        const AssetRegistry& registry) const {
        static_cast<void>(metadata);
        static_cast<void>(registry);
        return std::nullopt;
    }
    // Runtime validation receives the complete load request so validators that inspect source
    // documents can read them from a mounted package. The metadata-only overload remains the
    // discovery/editor contract and is the default for validators that need no source bytes.
    [[nodiscard]] virtual std::optional<std::string> ValidateRuntimeDependencies(
        const AssetLoadRequest& request,
        const AssetRegistry& registry) const {
        return ValidateDependencies(request.metadata, registry);
    }
    // Discovery-time-only free-text tag a loader can offer for its own asset type (e.g. a particle
    // recipe's authored category), stored into AssetMetadata.browseTag - a search-only field, NOT
    // the closed importCategory vocabulary - so browsers can search/filter on it without ever
    // calling the full, cache-mutating Load() per row. Skipped when the file's content hash is
    // unchanged since the previous scan (see AssetDiscoveryService), so it is not necessarily
    // "cheap" per call for loaders backed by a full document parse, but its steady-state cost is
    // bounded to actual file changes rather than every scan. Default is empty (no tag), matching
    // every loader that has nothing to offer here.
    [[nodiscard]] virtual std::string DiscoverBrowseTag(const std::filesystem::path& path) const {
        static_cast<void>(path);
        return {};
    }
};

} // namespace kb::assets
